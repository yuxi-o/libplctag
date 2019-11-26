
#include <stdlib.h>
#include <string.h>
#include "log.h"
#include "tags.h"


tag_data *tags = NULL;
size_t num_tags = 0;

void init_tags()
{
	int i=0;
	int *pint, *pintmore;
	float *pfloat;

    num_tags = 4;

    tags = (tag_data *)calloc(num_tags, sizeof(tag_data));

    tags[0].name = "TestDINTArray";
    tags[0].data_type[0] = 0xc4;
    tags[0].data_type[1] = 0x00;
    tags[0].elem_count = 10;
    tags[0].elem_size = 4;
    tags[0].data = (uint8_t *)calloc(tags[0].elem_size, tags[0].elem_count);
	pint =(int *) tags[0].data;
	for(i=0; i<tags[0].elem_count; i++){
		*(pint + i) = 100 + i;
	}

    tags[1].name = "TestBigArray";
    tags[1].data_type[0] = 0xc4;
    tags[1].data_type[1] = 0x00;
    tags[1].elem_count = 1000;
    tags[1].elem_size = 4;
    tags[1].data = (uint8_t *)calloc(tags[1].elem_size, tags[1].elem_count);
	pintmore = (int *)tags[1].data;
	for(i=0; i<tags[1].elem_count; i++){
		*(pintmore + i) = 100 + i;
	}

	tags[2].name = "TestREALArray";
    tags[2].data_type[0] = 0xca;
    tags[2].data_type[1] = 0x00;
    tags[2].elem_count = 20;
    tags[2].elem_size = 4;
    tags[2].data = (uint8_t *)calloc(tags[2].elem_size, tags[2].elem_count);
	pfloat = (float *)tags[2].data;
	for(i=0; i<tags[2].elem_count; i++){
		*(pfloat + i) =100 + i + i*0.01;
	}

	tags[3].name = "TestBOOL";
    tags[3].data_type[0] = 0xc1;
    tags[3].data_type[1] = 0x01;
    tags[3].elem_count = 1;
    tags[3].elem_size = 1;
    tags[3].data = (uint8_t *)calloc(tags[3].elem_size, tags[3].elem_count);
	*tags[3].data = 0x1 << 1;

}


tag_data *find_tag(const char *tag_name)
{
    log("find_data() finding tag %s\n", tag_name);

    for(size_t i=0; i<num_tags; i++) {
        if(strcmp(tags[i].name, tag_name) == 0) {
            return &(tags[i]);
        }
    }

    log("find_tag() unable to find tag %s\n", tag_name);

    return NULL;
}
