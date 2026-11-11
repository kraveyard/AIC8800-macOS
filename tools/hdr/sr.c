#include <stdio.h>
#include <stddef.h>
#include "lmac_msg.h"
int main(void){printf("hdr=%zu payload_off=%zu\n", sizeof(struct scanu_result_ind), offsetof(struct scanu_result_ind, payload));return 0;}
