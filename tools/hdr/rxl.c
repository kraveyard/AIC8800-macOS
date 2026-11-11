#include <stdio.h>
#include <stddef.h>
#define __packed __attribute__((packed))
#include "lmac_msg.h"
struct sk_buff; struct rwnx_hw; struct rwnx_vif; struct rwnx_sta; struct net_device; struct list_head{void*a,*b;}; typedef int spinlock_t;
#include "rwnx_rx.h"
int main(void){
 printf("sizeof hw_rxhdr=%zu hw_vect=%zu rx_vector_1=%zu\n", sizeof(struct hw_rxhdr), sizeof(struct hw_vect), sizeof(struct rx_vector_1));
 printf("hwvect.tsf_lo@%zu rx_vect1@%zu rx_vect2@%zu phy_info@%zu\n", offsetof(struct hw_rxhdr,hwvect.tsf_lo), offsetof(struct hw_rxhdr,hwvect.rx_vect1), offsetof(struct hw_rxhdr,hwvect.rx_vect2), offsetof(struct hw_rxhdr,phy_info));
 return 0;}
