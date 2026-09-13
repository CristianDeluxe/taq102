#!/usr/bin/env python3
"""Exercise the patch's actual charger callbacks with a fake cached regmap.

This checks driver logic and the volatile declaration, not kernel integration.
"""
from pathlib import Path
import re
import subprocess
import tempfile


def main():
    with tempfile.TemporaryDirectory(prefix="taq102-input-current-") as directory:
        root = Path(directory)
        patch = Path(__file__).resolve().parents[1] / "kernel/mainline/0008-power-supply-add-the-RK816-battery-and-charger.patch"
        text = patch.read_text()
        section = text.split("+++ b/drivers/power/supply/rk816_charger.c\n", 1)[1]
        source = "\n".join(line[1:] for line in section.splitlines() if line.startswith("+"))
        mfd = text.split("+++ b/drivers/mfd/rk8xx-i2c.c\n", 1)[1].split("diff --git", 1)[0]
        assert "+\tcase RK816_USB_CTRL_REG:" in mfd
        functions = []
        for name in ('rk816_bat_set_input_current', 'rk816_bat_get_input_current', 'rk816_usb_get_prop', 'rk816_usb_set_prop', 'rk816_plug_out_isr'):
            functions.append(re.search(r'static (?:int|irqreturn_t) ' + name + r'\(.*?\n\}', source, re.S).group())
        prelude = r'''
        #include <assert.h>
        #include <stdbool.h>
        #include <stdint.h>
        #include <stdio.h>
        #include <errno.h>
        typedef uint32_t u32;
        typedef int irqreturn_t;
        #define RK816_USB_CTRL_REG 0xa1
        #define RK808_VB_MON_REG 0x21
        #define RK816_PLUG_IN_STS 0x40
        #define RK816_USB_INPUT_CUR_MSK 0xf
        #define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
        #define FIELD_PREP(mask, x) ((x) & (mask))
        #define FIELD_GET(mask, x) ((x) & (mask))
        #define dev_warn(...) ((void)0)
        static const unsigned int rk816_input_cur_ma[] = {450,80,850,1000,1250,1500,1750,2000};
        struct regmap { unsigned int hardware, cache, vbus, writes; int fail_read, fail_write; bool usb_volatile; };
        struct rk816_charger { struct regmap *regmap; int lock; u32 input_current_override_ua; bool plugged_in, usb_online; };
        struct power_supply { struct rk816_charger *cg; };
        enum power_supply_property { POWER_SUPPLY_PROP_ONLINE, POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT };
        union power_supply_propval { int intval; };
        static void mutex_lock(int *lock) { assert(!*lock); *lock = 1; }
        static void mutex_unlock(int *lock) { assert(*lock); *lock = 0; }
        static struct rk816_charger *power_supply_get_drvdata(struct power_supply *ps) { return ps->cg; }
        static int regmap_read(struct regmap *m, unsigned int reg, unsigned int *v) {
            if (m->fail_read) return -EIO;
            *v = reg == RK808_VB_MON_REG ? m->vbus : (m->usb_volatile ? m->hardware : m->cache);
            return 0;
        }
        static int update(struct regmap *m, unsigned int reg, unsigned int mask, unsigned int v, bool force) {
            unsigned int old; int ret = regmap_read(m, reg, &old);
            if (ret) return ret;
            unsigned int next = (old & ~mask) | (v & mask);
            if (force || next != old) {
                if (m->fail_write) return -EIO;
                m->hardware = next; m->cache = next; m->writes++;
            }
            return 0;
        }
        static int regmap_write_bits(struct regmap *m, unsigned int r, unsigned int mask, unsigned int v) { return update(m,r,mask,v,true); }
        static int rk816_plug_isr(int irq, void *data) { (void)irq; (void)data; return 1; }
        '''
        tests = r'''
        int main(void) {
            struct regmap m = {.hardware=0x40, .cache=0x45, .vbus=0x40};
            struct rk816_charger cg = {.regmap=&m};
            struct power_supply ps = {.cg=&cg};
            union power_supply_propval val = {.intval=1500000};
            /* Reproduce the old cached read and suppressed masked write. */
            assert(!rk816_usb_get_prop(&ps,POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,&val));
            assert(val.intval==1500000);
            assert(!update(&m,0xa1,0xf,5,false)); assert(m.hardware==0x40 && m.writes==0);
            puts("PASS: old cached behavior reproduces 1500000 readback with hardware 0x40 and no write");
            m.usb_volatile=true;
            assert(!rk816_usb_get_prop(&ps,POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,&val)); assert(val.intval==450000);
            val.intval=1500000;
            assert(!rk816_usb_set_prop(&ps,POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,&val));
            assert(m.hardware==0x45 && m.writes==1 && cg.input_current_override_ua==1500000 && cg.plugged_in);
            assert(!cg.usb_online); /* No extcon state was needed. */
            assert(!rk816_usb_set_prop(&ps,POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,&val)); assert(m.writes==2);
            puts("PASS: live getter, immediate override without extcon, and same-value write");
            m.hardware=0xb0; m.cache=0x45; val.intval=1600000;
            assert(!rk816_usb_set_prop(&ps,POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,&val)); assert(m.hardware==0xb5);
            assert(!rk816_usb_get_prop(&ps,POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,&val)); assert(val.intval==1500000);
            const int requests[]={80000,81000,449999,450000,849999,850000,999999,1000000,1250000,1500000,1750000,2000000};
            const int expected[]={1,1,1,0,0,2,2,3,4,5,6,7};
            for (unsigned int i=0;i<ARRAY_SIZE(requests);i++) {
                val.intval=requests[i]; assert(!rk816_usb_set_prop(&ps,POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,&val));
                assert(m.hardware==(unsigned int)(0xb0|expected[i]));
            }
            val.intval=79999; assert(rk816_usb_set_prop(&ps,POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,&val)==-EINVAL);
            val.intval=-1; assert(rk816_usb_set_prop(&ps,POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,&val)==-EINVAL);
            puts("PASS: live VLIM preservation, all selector boundaries, rounded hardware readback, invalid requests");
            cg.input_current_override_ua=1500000; m.vbus=0; unsigned int writes=m.writes;
            val.intval=1500000;
            assert(rk816_usb_set_prop(&ps,POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,&val)==-ENODEV);
            assert(!cg.input_current_override_ua && m.writes==writes && !cg.lock);
            m.vbus=0x40; m.fail_read=1;
            assert(rk816_usb_set_prop(&ps,POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,&val)==-EIO && !cg.lock);
            assert(rk816_usb_get_prop(&ps,POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,&val)==-EIO);
            m.fail_read=0; m.fail_write=1;
            assert(rk816_usb_set_prop(&ps,POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,&val)==-EIO && !cg.lock && !cg.input_current_override_ua);
            m.fail_write=0;
            assert(!rk816_usb_set_prop(&ps,POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,&val));
            assert(rk816_plug_out_isr(1,&cg)==1);
            assert(!cg.input_current_override_ua && m.hardware==0xb0 && !cg.lock);
            puts("PASS: unplugged rejection, I/O error propagation, unlock paths, unplug IRQ expiry after quick VBUS return");
        }
        '''
        (root / 'input-current-test.c').write_text(prelude + '\n'.join(functions) + tests)
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(root / 'input-current-test.c'),'-o',str(root / 'input-current-test')], check=True)
        subprocess.run([str(root / 'input-current-test')],check=True)
        print('PASS: USB_CTRL classified volatile in the applied MFD patch')


if __name__ == "__main__":
    main()
