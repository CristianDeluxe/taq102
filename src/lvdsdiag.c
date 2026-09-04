// Exercise named RK3126 LVDS PHY configurations from a known DRM modeset.
//
// The tool owns one dumb framebuffer and never flips it. Every invocation
// performs an on/off/on modeset before touching PHY registers, so the kernel
// driver gets a complete power-off/power-on cycle and establishes its known
// baseline first.
//
//   lvdsdiag <variant> [pattern]
//
// Variants: ours, vendor, forward, pll336, msbsel, msbsel-off, stock-order,
//           source-e4
// Patterns: vlines, hlines, checker, grey, white, black, bars

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

#define PHY_BASE 0x20038000u
#define GRF_BASE 0x20008000u
#define HOST_BASE 0x10110000u
#define MAP_SIZE 0x1000u

#define ANALOG_REG00 0x000u
#define ANALOG_REG01 0x004u
#define ANALOG_REG03 0x00cu
#define ANALOG_REG04 0x010u
#define ANALOG_REG05 0x014u
#define ANALOG_REG08 0x020u

#define LVDS_REGE0 0x380u
#define LVDS_REGE1 0x384u
#define LVDS_REGE3 0x38cu
#define LVDS_REGE4 0x390u
#define LVDS_REGE8 0x3a0u
#define LVDS_REGEB 0x3acu

#define GRF_LVDS_CON0 0x150u
#define DSI_PHY_STATUS 0x0b0u

#define ANALOG_BANDGAP_DOWN 0x80u
#define ANALOG_LANES 0x7cu
#define ANALOG_POWER_WORK 0x03u
#define ANALOG_POWER_WORK_ENABLE 0x01u
#define ANALOG_POWER_WORK_DISABLE 0x02u
#define ANALOG_SYNCRST 0x04u
#define ANALOG_LDO_PLL_DOWN 0x03u
#define SAMPLE_DIRECTION_REVERSE 0x10u

#define LVDS_INTERNAL_MSB 0x01u
#define LVDS_INTERNAL_RESET 0x04u
#define LVDS_DIGITAL_ENABLE 0x80u
#define LVDS_MODE 0x02u
#define LVDS_LANES 0xf8u
#define LVDS_PLL_BANDGAP_DOWN 0x05u

static volatile sig_atomic_t stop;

struct registers {
    volatile uint32_t *phy;
    volatile uint32_t *grf;
    volatile uint32_t *host;
};

struct display {
    int fd;
    drmModeRes *resources;
    drmModeConnector *connector;
    drmModeModeInfo mode;
    uint32_t crtc_id;
    uint32_t framebuffer_id;
    uint32_t handle;
    uint32_t pitch;
    uint64_t size;
    uint32_t *pixels;
};

enum variant {
    VARIANT_OURS,
    VARIANT_VENDOR,
    VARIANT_FORWARD,
    VARIANT_PLL336,
    VARIANT_MSBSEL,
    VARIANT_MSBSEL_OFF,
    VARIANT_STOCK_ORDER,
    VARIANT_SOURCE_E4,
};

static void on_signal(int signal_number)
{
    (void)signal_number;
    stop = 1;
}

static uint32_t read_register(volatile uint32_t *base, unsigned int offset)
{
    return base[offset / sizeof(uint32_t)];
}

static void write_register(volatile uint32_t *base, unsigned int offset,
                           uint32_t value)
{
    base[offset / sizeof(uint32_t)] = value;
    __sync_synchronize();
    (void)read_register(base, offset);
}

static void update_register(volatile uint32_t *base, unsigned int offset,
                            uint32_t mask, uint32_t value)
{
    uint32_t current = read_register(base, offset);

    write_register(base, offset, (current & ~mask) | (value & mask));
}

static void dump_registers(const struct registers *registers, const char *label)
{
    printf("%s: A00=%02x A01=%02x A03=%02x A04=%02x A05=%02x A08=%02x "
           "E0=%02x E1=%02x E3=%02x E4=%02x E8=%02x EB=%02x "
           "GRF=%04x STATUS=%04x\n",
           label,
           read_register(registers->phy, ANALOG_REG00),
           read_register(registers->phy, ANALOG_REG01),
           read_register(registers->phy, ANALOG_REG03),
           read_register(registers->phy, ANALOG_REG04),
           read_register(registers->phy, ANALOG_REG05),
           read_register(registers->phy, ANALOG_REG08),
           read_register(registers->phy, LVDS_REGE0),
           read_register(registers->phy, LVDS_REGE1),
           read_register(registers->phy, LVDS_REGE3),
           read_register(registers->phy, LVDS_REGE4),
           read_register(registers->phy, LVDS_REGE8),
           read_register(registers->phy, LVDS_REGEB),
           read_register(registers->grf, GRF_LVDS_CON0),
           read_register(registers->host, DSI_PHY_STATUS));
    fflush(stdout);
}

static bool wait_for_status(const struct registers *registers)
{
    int attempt;

    for (attempt = 0; attempt < 200; attempt++) {
        if (read_register(registers->host, DSI_PHY_STATUS) & 1u)
            return true;
        usleep(50);
    }

    return false;
}

static void quiesce_phy(const struct registers *registers)
{
    update_register(registers->phy, LVDS_REGE1, LVDS_DIGITAL_ENABLE, 0);
    update_register(registers->phy, LVDS_REGEB, LVDS_LANES, 0);
    update_register(registers->phy, ANALOG_REG00, ANALOG_LANES, 0);
    update_register(registers->phy, LVDS_REGEB,
                    LVDS_PLL_BANDGAP_DOWN, LVDS_PLL_BANDGAP_DOWN);
    update_register(registers->phy, ANALOG_REG01,
                    ANALOG_LDO_PLL_DOWN, ANALOG_LDO_PLL_DOWN);
    update_register(registers->phy, ANALOG_REG00,
                    ANALOG_BANDGAP_DOWN | ANALOG_POWER_WORK,
                    ANALOG_BANDGAP_DOWN | ANALOG_POWER_WORK_DISABLE);
    usleep(1000);
}

static void configure_pll(const struct registers *registers,
                          unsigned int prediv, unsigned int fbdiv,
                          bool full_write)
{
    uint32_t reg03 = (prediv & 0x1fu) | (((fbdiv >> 8) & 1u) << 5);

    if (full_write)
        write_register(registers->phy, ANALOG_REG03, reg03);
    else
        update_register(registers->phy, ANALOG_REG03, 0x3fu, reg03);
    write_register(registers->phy, ANALOG_REG04, fbdiv & 0xffu);
}

static void apply_combo_sequence(const struct registers *registers,
                                 unsigned int prediv, unsigned int fbdiv,
                                 bool reverse, int internal_msb, uint32_t e4)
{
    quiesce_phy(registers);

    update_register(registers->phy, ANALOG_REG00,
                    ANALOG_BANDGAP_DOWN | ANALOG_POWER_WORK,
                    ANALOG_POWER_WORK_ENABLE);
    update_register(registers->phy, ANALOG_REG08,
                    SAMPLE_DIRECTION_REVERSE,
                    reverse ? SAMPLE_DIRECTION_REVERSE : 0);
    write_register(registers->phy, ANALOG_REG05, 0x50u);
    update_register(registers->phy, LVDS_REGE3, 0x07u, LVDS_MODE);
    configure_pll(registers, prediv, fbdiv, false);
    write_register(registers->phy, LVDS_REGE8, 0xfcu);
    write_register(registers->phy, LVDS_REGE4, e4);
    update_register(registers->phy, ANALOG_REG01,
                    ANALOG_LDO_PLL_DOWN, 0);
    update_register(registers->phy, ANALOG_REG01,
                    ANALOG_SYNCRST, ANALOG_SYNCRST);
    usleep(1);
    update_register(registers->phy, ANALOG_REG01, ANALOG_SYNCRST, 0);
    update_register(registers->phy, LVDS_REGEB,
                    LVDS_PLL_BANDGAP_DOWN, 0);

    (void)wait_for_status(registers);

    update_register(registers->phy, LVDS_REGE0, LVDS_INTERNAL_RESET, 0);
    usleep(1);
    update_register(registers->phy, LVDS_REGE0,
                    LVDS_INTERNAL_RESET, LVDS_INTERNAL_RESET);
    if (internal_msb >= 0)
        update_register(registers->phy, LVDS_REGE0, LVDS_INTERNAL_MSB,
                        internal_msb ? LVDS_INTERNAL_MSB : 0);
    update_register(registers->phy, LVDS_REGE1,
                    LVDS_DIGITAL_ENABLE, LVDS_DIGITAL_ENABLE);
    update_register(registers->phy, LVDS_REGEB, LVDS_LANES, LVDS_LANES);
    update_register(registers->phy, ANALOG_REG00,
                    ANALOG_LANES, ANALOG_LANES);
}

static void apply_stock_sequence(const struct registers *registers,
                                 unsigned int prediv, unsigned int fbdiv,
                                 bool reverse, bool vendor_analog)
{
    quiesce_phy(registers);

    if (vendor_analog) {
        write_register(registers->phy, ANALOG_REG00, 0x01u);
        write_register(registers->phy, ANALOG_REG08, 0x4cu);
    } else {
        update_register(registers->phy, ANALOG_REG00,
                        ANALOG_BANDGAP_DOWN | ANALOG_POWER_WORK,
                        ANALOG_POWER_WORK_ENABLE);
        update_register(registers->phy, ANALOG_REG08,
                        SAMPLE_DIRECTION_REVERSE,
                        reverse ? SAMPLE_DIRECTION_REVERSE : 0);
    }
    write_register(registers->phy, ANALOG_REG05, 0x50u);

    update_register(registers->phy, LVDS_REGE1, LVDS_DIGITAL_ENABLE, 0);
    configure_pll(registers, prediv, fbdiv, true);
    write_register(registers->phy, LVDS_REGE8, 0xfcu);
    update_register(registers->phy, LVDS_REGE0,
                    LVDS_INTERNAL_MSB | LVDS_INTERNAL_RESET,
                    LVDS_INTERNAL_MSB | LVDS_INTERNAL_RESET);
    if (vendor_analog)
        write_register(registers->phy, LVDS_REGE4, 0x8au);
    else
        write_register(registers->phy, LVDS_REGE4, 0xaau);
    update_register(registers->phy, ANALOG_REG01,
                    ANALOG_SYNCRST | ANALOG_LDO_PLL_DOWN, 0);
    write_register(registers->phy, LVDS_REGEB, LVDS_LANES);
    update_register(registers->phy, LVDS_REGE3, 0x07u, LVDS_MODE);

    (void)wait_for_status(registers);

    update_register(registers->phy, LVDS_REGE1,
                    LVDS_DIGITAL_ENABLE, LVDS_DIGITAL_ENABLE);
    if (!vendor_analog)
        update_register(registers->phy, ANALOG_REG00,
                        ANALOG_LANES, ANALOG_LANES);
}

static enum variant parse_variant(const char *name)
{
    if (strcmp(name, "ours") == 0)
        return VARIANT_OURS;
    if (strcmp(name, "vendor") == 0)
        return VARIANT_VENDOR;
    if (strcmp(name, "forward") == 0)
        return VARIANT_FORWARD;
    if (strcmp(name, "pll336") == 0)
        return VARIANT_PLL336;
    if (strcmp(name, "msbsel") == 0)
        return VARIANT_MSBSEL;
    if (strcmp(name, "msbsel-off") == 0)
        return VARIANT_MSBSEL_OFF;
    if (strcmp(name, "stock-order") == 0)
        return VARIANT_STOCK_ORDER;
    if (strcmp(name, "source-e4") == 0)
        return VARIANT_SOURCE_E4;

    fprintf(stderr, "unknown variant: %s\n", name);
    exit(EXIT_FAILURE);
}

static bool valid_pattern(const char *name)
{
    static const char *const names[] = {
        "vlines", "hlines", "checker", "grey", "white", "black", "bars",
    };
    size_t index;

    for (index = 0; index < sizeof(names) / sizeof(names[0]); index++) {
        if (strcmp(name, names[index]) == 0)
            return true;
    }

    return false;
}

static void paint_pattern(uint32_t *pixels, int width, int height,
                          int pitch_pixels, const char *pattern)
{
    int x;
    int y;

    for (y = 0; y < height; y++) {
        uint32_t *row = pixels + (size_t)y * (size_t)pitch_pixels;

        for (x = 0; x < width; x++) {
            uint32_t color;

            if (strcmp(pattern, "vlines") == 0)
                color = (x & 1) ? 0xff000000u : 0xffffffffu;
            else if (strcmp(pattern, "hlines") == 0)
                color = (y & 1) ? 0xff000000u : 0xffffffffu;
            else if (strcmp(pattern, "checker") == 0)
                color = ((x ^ y) & 1) ? 0xff000000u : 0xffffffffu;
            else if (strcmp(pattern, "grey") == 0)
                color = 0xff808080u;
            else if (strcmp(pattern, "white") == 0)
                color = 0xffffffffu;
            else if (strcmp(pattern, "black") == 0)
                color = 0xff000000u;
            else {
                int band = x / 256;

                if (band == 0)
                    color = (x & 1) ? 0xff000000u : 0xffffffffu;
                else if (band == 1)
                    color = (y & 1) ? 0xff000000u : 0xffffffffu;
                else if (band == 2)
                    color = 0xff808080u;
                else
                    color = 0xffffffffu;
            }
            row[x] = color;
        }
    }
}

static volatile uint32_t *map_register_page(int memory_fd, off_t address)
{
    void *mapping = mmap(NULL, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
                         memory_fd, address);

    if (mapping == MAP_FAILED) {
        perror("mmap registers");
        exit(EXIT_FAILURE);
    }

    return mapping;
}

static struct registers open_registers(void)
{
    struct registers registers;
    int memory_fd = open("/dev/mem", O_RDWR | O_SYNC);

    if (memory_fd < 0) {
        perror("open /dev/mem");
        exit(EXIT_FAILURE);
    }

    registers.phy = map_register_page(memory_fd, PHY_BASE);
    registers.grf = map_register_page(memory_fd, GRF_BASE);
    registers.host = map_register_page(memory_fd, HOST_BASE);
    close(memory_fd);
    return registers;
}

static drmModeConnector *find_connector(int fd, drmModeRes *resources)
{
    int index;

    for (index = 0; index < resources->count_connectors; index++) {
        drmModeConnector *connector =
            drmModeGetConnector(fd, resources->connectors[index]);

        if (connector != NULL &&
            connector->connection == DRM_MODE_CONNECTED &&
            connector->count_modes > 0)
            return connector;
        drmModeFreeConnector(connector);
    }

    return NULL;
}

static struct display open_display(const char *pattern)
{
    struct display display = {0};
    struct drm_mode_create_dumb create = {0};
    struct drm_mode_map_dumb map = {0};
    drmModeEncoder *encoder;

    display.fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (display.fd < 0) {
        perror("open /dev/dri/card0");
        exit(EXIT_FAILURE);
    }

    display.resources = drmModeGetResources(display.fd);
    if (display.resources == NULL) {
        perror("drmModeGetResources");
        exit(EXIT_FAILURE);
    }

    display.connector = find_connector(display.fd, display.resources);
    if (display.connector == NULL) {
        fprintf(stderr, "no connected DRM connector\n");
        exit(EXIT_FAILURE);
    }

    display.mode = display.connector->modes[0];
    encoder = drmModeGetEncoder(display.fd, display.connector->encoder_id);
    display.crtc_id = encoder != NULL ? encoder->crtc_id
                                      : display.resources->crtcs[0];
    drmModeFreeEncoder(encoder);

    create.width = (uint32_t)display.mode.hdisplay;
    create.height = (uint32_t)display.mode.vdisplay;
    create.bpp = 32;
    if (drmIoctl(display.fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) < 0) {
        perror("DRM_IOCTL_MODE_CREATE_DUMB");
        exit(EXIT_FAILURE);
    }

    display.handle = create.handle;
    display.pitch = create.pitch;
    display.size = create.size;
    if (drmModeAddFB(display.fd, create.width, create.height, 24, 32,
                     create.pitch, create.handle,
                     &display.framebuffer_id) != 0) {
        perror("drmModeAddFB");
        exit(EXIT_FAILURE);
    }

    map.handle = create.handle;
    if (drmIoctl(display.fd, DRM_IOCTL_MODE_MAP_DUMB, &map) < 0) {
        perror("DRM_IOCTL_MODE_MAP_DUMB");
        exit(EXIT_FAILURE);
    }

    display.pixels = mmap(NULL, create.size, PROT_READ | PROT_WRITE,
                          MAP_SHARED, display.fd, map.offset);
    if (display.pixels == MAP_FAILED) {
        perror("mmap framebuffer");
        exit(EXIT_FAILURE);
    }

    paint_pattern(display.pixels, display.mode.hdisplay, display.mode.vdisplay,
                  (int)(display.pitch / sizeof(uint32_t)), pattern);
    return display;
}

static void perform_clean_modeset(struct display *display)
{
    uint32_t connector_id = display->connector->connector_id;

    if (drmModeSetCrtc(display->fd, display->crtc_id,
                       display->framebuffer_id, 0, 0,
                       &connector_id, 1, &display->mode) != 0) {
        perror("initial drmModeSetCrtc");
        exit(EXIT_FAILURE);
    }
    usleep(200000);

    if (drmModeSetCrtc(display->fd, display->crtc_id, 0, 0, 0,
                       NULL, 0, NULL) != 0) {
        perror("disable drmModeSetCrtc");
        exit(EXIT_FAILURE);
    }
    usleep(100000);

    if (drmModeSetCrtc(display->fd, display->crtc_id,
                       display->framebuffer_id, 0, 0,
                       &connector_id, 1, &display->mode) != 0) {
        perror("clean drmModeSetCrtc");
        exit(EXIT_FAILURE);
    }
    usleep(200000);
}

static void apply_variant(const struct registers *registers,
                          enum variant variant)
{
    switch (variant) {
    case VARIANT_OURS:
        apply_combo_sequence(registers, 12, 175, true, 1, 0xaau);
        return;
    case VARIANT_VENDOR:
        apply_stock_sequence(registers, 2, 28, false, true);
        return;
    case VARIANT_FORWARD:
        apply_combo_sequence(registers, 12, 175, false, 1, 0xaau);
        return;
    case VARIANT_PLL336:
        apply_combo_sequence(registers, 2, 28, true, 1, 0xaau);
        return;
    case VARIANT_MSBSEL:
        apply_combo_sequence(registers, 12, 175, true, 1, 0xaau);
        return;
    case VARIANT_MSBSEL_OFF:
        apply_combo_sequence(registers, 12, 175, true, 0, 0xaau);
        return;
    case VARIANT_STOCK_ORDER:
        apply_stock_sequence(registers, 12, 175, true, false);
        return;
    case VARIANT_SOURCE_E4:
        apply_combo_sequence(registers, 12, 175, true, 1, 0x80u);
        return;
    }
}

static void usage(const char *program)
{
    fprintf(stderr,
            "usage: %s <ours|vendor|forward|pll336|msbsel|msbsel-off|stock-order|source-e4> "
            "[vlines|hlines|checker|grey|white|black|bars]\n",
            program);
}

int main(int argc, char **argv)
{
    const char *pattern;
    enum variant variant;
    struct display display;
    struct registers registers;

    if (argc < 2 || argc > 3) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    pattern = argc == 3 ? argv[2] : "bars";
    if (!valid_pattern(pattern)) {
        fprintf(stderr, "unknown pattern: %s\n", pattern);
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    variant = parse_variant(argv[1]);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    registers = open_registers();
    display = open_display(pattern);
    perform_clean_modeset(&display);
    dump_registers(&registers, "driver-baseline");
    apply_variant(&registers, variant);
    dump_registers(&registers, argv[1]);

    printf("lvdsdiag: holding %s with variant %s; re-run as 'lvdsdiag ours %s' "
           "to restore the driver baseline\n",
           pattern, argv[1], pattern);
    fflush(stdout);
    while (!stop)
        sleep(1);

    return EXIT_SUCCESS;
}
