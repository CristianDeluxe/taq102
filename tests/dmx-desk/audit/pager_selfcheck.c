#include "api.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

void audit_pager_selfcheck(struct audit_context *a) {
    struct audit_context *copy=malloc(sizeof *copy);assert(copy);
    *copy=*a;
    copy->report=tmpfile();assert(copy->report);
    copy->failures=0;copy->warnings=0;
    copy->inject_pager_dead_strip=1;
    audit_inventory(copy);
    audit_scan(copy);
    rewind(copy->report);
    char line[2048];int detected=0;
    while(fgets(line,sizeof line,copy->report))
        if(strstr(line,"[dead-band]")&&strstr(line,"edge=yes")&&strstr(line,",584,")&&strstr(line,",16)"))detected=1;
    fprintf(a->report,"SELF-CHECK %s: historical 16 px pager strip remains an edge-connected dead-band failure\n",detected?"PASS":"FAIL");
    if(!detected)audit_note(a,0,"detector-self-check","historical pager strip escaped the inert contract");
    fclose(copy->report);free(copy);
}
