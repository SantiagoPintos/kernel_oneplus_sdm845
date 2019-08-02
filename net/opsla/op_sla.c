/* Copyright (c) 2019 OnePlus. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <op_sla.h>
#include <linux/types.h>
#include <linux/ip.h>
#include <linux/netfilter.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/icmp.h>
#include <linux/sysctl.h>
#include <net/route.h>
#include <net/ip.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/version.h>
#include <net/tcp.h>
#include <linux/random.h>
#include <net/sock.h>
#include <net/dst.h>
#include <linux/file.h>
#include <net/tcp_states.h>
#include <linux/netlink.h>
#include <net/sch_generic.h>
#include <net/pkt_sched.h>
#include <net/netfilter/nf_queue.h>
#include <linux/netfilter/xt_state.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_owner.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/ipv4/nf_conntrack_ipv4.h>

#define MARK_MASK	 0x0fff
#define RETRAN_MASK	 0xf000
#define RTT_MASK	 0xf000
#define GAME_UNSPEC_MASK 0x8000

#define WLAN_INDEX	0
#define CELLULAR_INDEX	1
#define DOWN_LOAD_FLAG	5
#define MAX_SYN_RETRANS	5
#define RTT_NUM		5

#define NORMAL_RTT	100
#define BACK_OFF_RTT_1	200   //200ms
#define BACK_OFF_RTT_2	350   //300ms
#define SYN_RETRAN_RTT	300   //500ms
#define MAX_RTT		500

#define CALC_DEV_SPEED_TIME 1
#define RECALC_WEIGHT_TIME  5    //5 seconds to recalc weight
#define MAX_SYN_NEW_COUNT   10   //statitis new syn count max number
#define LITTLE_FLOW_TIME    60   //small flow detect time internal

#define DOWNLOAD_SPEED      200   // download speed level min
#define MAX_CELLULAR_SPEED  300   //LTE CALC WEITHT MAX SPEED
#define MAX_WLAN_SPEED      1000  //WIFI CALC WEITHT MAX SPEED
#define LOW_RSSI_MAX_WLAN_SPEED 500

#define CALC_WEIGHT_MIN_SPEED_1 10  //10KB/s
#define CALC_WEIGHT_MIN_SPEED_2 50  //50KB/s
#define CALC_WEIGHT_MIN_SPEED_3 100 //100KB/s

#define IFACE_NUM 2
#define IFACE_LEN 16

#define GAME_BASE   1
#define GAME_NUM   64
#define WHITE_APP_BASE    100
#define WHITE_APP_NUM      64

#define INIT_APP_TYPE       0
#define GAME_TYPE           1
#define WHITE_APP_TYPE      2

#define MAX_GAME_RTT      300
#define SLA_TIMER_EXPIRES HZ
#define MINUTE_LITTE_RATE  60     //60Kbit/s
#define WLAN_SCORE_BAD_NUM 10
#define GAME_SKB_TIME_OUT  120    //120s
#define WLAN_SCORE_GOOD    65
#define WLAN_SCORE_BAD     55
#define CELL_SCORE_BAD     -100
#define ENABLE_TO_USER_TIMEOUT 5 //second
#define MAX_RTT_RECORD_NUM 4
#define GAME_LINK_SWITCH_TIME (10 * 60 * 1000) //10minutes
#define PINGPONG_AVOID_TIME (60 * 60 * 1000) //60minutes

// dev info struct
// if we need to consider wifi RSSI? If we need consider screen state?
struct op_dev_info {
	bool need_up;
	bool need_disable;
	int max_speed;    //KB/S
	int current_speed;
	int left_speed;
	int minute_speed; //kbit/s
	int download_flag;
	int congestion_flag;
	int if_up;
	int syn_retran;
	int wlan_score;
	int wlan_score_bad_count;
	int cell_score;
	int weight;
	int weight_state;
	int rtt_index;
	int rtt_congestion_count;
	int netlink_valid;
	u32 mark;
	u32 avg_rtt;
	u32 sum_rtt;         //ms
	u32 sla_rtt_num;     //ms
	u32 sla_sum_rtt;     //ms
	u32 sla_avg_rtt;     //ms
	u64 rx_bytes;
	u64 minute_rx_bytes;
	char dev_name[IFACE_LEN];
};

struct op_speed_calc {
	int speed;
	int speed_done;
	int ms_speed_flag;
	u64 rx_bytes;
	u64 bytes_time;
	u64 first_time;
	u64 last_time;
	u64 sum_bytes;
};

struct op_white_app_info {
	u32 count;
	u32 uid[WHITE_APP_NUM];
	u64 cell_bytes[WHITE_APP_NUM];
	u64 cell_bytes_normal[WHITE_APP_NUM];
};

struct op_game_app_info {
	u32 count;
	u32 game_type[GAME_NUM];
	u32 uid[GAME_NUM];
	int rtt[GAME_NUM];
	u32 special_rx_error_count[GAME_NUM];
	u32 special_rx_count[GAME_NUM];
	u32 mark[GAME_NUM];
	u32 switch_time[GAME_NUM];
	u32 switch_count[GAME_NUM];
	u32 repeat_switch_time[GAME_NUM];
	u32 rtt_150_num[GAME_NUM];
	u32 rtt_200_num[GAME_NUM];
	u32 rtt_250_num[GAME_NUM];
	u64 rtt_normal_num[GAME_NUM];
	u64 cell_bytes[GAME_NUM];
};

struct op_game_online {
	bool game_online;
	struct timeval last_game_skb_tv;
};

struct op_syn_retran_statistic {
	u32 syn_retran_num;
	u32 syn_total_num;
};

struct op_sla_params_info {
	u32 sla_speed;               //sla speed threshold
	u32 cell_speed;              //cell max speed threshold
	u32 wlan_speed;              //wlan max speed threshold;
	u32 wlan_little_score_speed; //wlan little score speed threshold;
	u32 sla_rtt;                 //sla rtt threshold
	u32 wzry_rtt;                //wzry rtt threshold
	u32 cjzc_rtt;                //cjzc rtt threshold
	u32 pubg_rtt;		     //pubg rtt threshold
	u32 qqcar_rtt;		     //qqcar rtt threshold
	u32 wlan_bad_score;          //wifi bad score threshold
	u32 wlan_good_score;         //wifi good socre threshold
};

enum {
	SLA_SKB_ACCEPT,
	SLA_SKB_CONTINUE,
	SLA_SKB_MARKED,
	SLA_SKB_REMARK,
	SLA_SKB_DROP,
};

enum {
	SLA_WEIGHT_NORMAL,
	SLA_WEIGHT_RECOVERY,
};

enum {
	WEIGHT_STATE_NORMAL,
	WEIGHT_STATE_USELESS,
	WEIGHT_STATE_RECOVERY,
};

enum {
	CONGESTION_LEVEL_NORMAL,
	CONGESTION_LEVEL_MIDDLE,
	CONGESTION_LEVEL_HIGH,
};

enum {
	WLAN_SCORE_LOW,
	WLAN_SCORE_HIGH,
};

enum {
	WLAN_MARK_BIT = 8, //WLAN mark value,mask 0x0fff
	WLAN_MARK = (1 << WLAN_MARK_BIT),

	CELLULAR_MARK_BIT = 9, //cellular mark value  mask 0x0fff
	CELLULAR_MARK = (1 << CELLULAR_MARK_BIT),

	RETRAN_BIT = 12, //first retran mark value,  mask 0xf000
	RETRAN_MARK = (1 << RETRAN_BIT),

	RETRAN_SECOND_BIT = 13, //second retran mark value, mask 0xf000
	RETRAN_SECOND_MARK = (1 << RETRAN_SECOND_BIT),

	RTT_MARK_BIT = 14, //one ct only statistic once rtt,mask 0xf000
	RTT_MARK = (1 << RTT_MARK_BIT),

	GAME_UNSPEC_MARK_BIT = 15, //mark game skb when game not start
	GAME_UNSPEC_MARK = (1 << GAME_UNSPEC_MARK_BIT),
};

/* NLMSG_MIN_TYPE is 0x10,so we start at 0x11 */
enum {
	SLA_NOTIFY_WIFI_SCORE = 0x11,
	SLA_NOTIFY_PID = 0x12,
	SLA_ENABLE = 0x13,
	SLA_DISABLE = 0x14,
	SLA_WIFI_UP = 0x15,
	SLA_CELLULAR_UP = 0x16,
	SLA_WIFI_DOWN = 0x17,
	SLA_CELLULAR_DOWN = 0x18,
	SLA_CELLULAR_NET_MODE = 0x19,
	SLA_NOTIFY_GAME_UID = 0x1A,
	SLA_NOTIFY_GAME_RTT = 0x1B,
	SLA_NOTIFY_WHITE_LIST_APP = 0x1C,
	SLA_ENABLED = 0x1D,
	SLA_DISABLED = 0x1E,
	SLA_ENABLE_GAME_RTT = 0x1F,
	SLA_DISABLE_GAME_RTT = 0x20,
	SLA_NOTIFY_SWITCH_STATE = 0x21,
	SLA_NOTIFY_SPEED_RTT = 0x22,
	SLA_SWITCH_GAME_NETWORK  = 0x23,
	SLA_NOTIFY_SCREEN_STATE	= 0x24,
	SLA_NOTIFY_CELL_SCORE = 0x25,
	SLA_SHOW_DIALOG_NOW = 0x26,
	SLA_NOTIFY_SHOW_DIALOG = 0x27,
	SLA_SEND_APP_TRAFFIC = 0x28,
	SLA_SEND_GAME_APP_STATISTIC = 0x29,
	SLA_GET_SYN_RETRAN_INFO = 0x2A,
	SLA_GET_SPEED_UP_APP = 0x2B,
	SLA_SET_DEBUG = 0x2C,
	SLA_NOTIFY_DEFAULT_NETWORK = 0x2D,
	SLA_NOTIFY_PARAMS = 0x2E,
	SLA_NOTIFY_GAME_STATE = 0x2F,
	SLA_ENABLE_LINK_TURBO = 0x30,
	SLA_DISABLE_LINK_TURBO = 0x31,
	SLA_SET_GAME_MARK = 0x32,
	SLA_SET_NETWORK_VALID = 0x33,
};

enum {
	GAME_RTT_DETECT_INITIAL = 0,
	GAME_SKB_COUNT_ENOUGH,
	GAME_RTT_DETECTED_STREAM,
};

enum {
	GAME_WZRY = 1,
	GAME_CJZC,
	GAME_PUBG,
	GAME_PUBG_TW,
	GAME_MOBILE_LEGENDS,
	GAME_AOV,
	GAME_JZPAJ,
	GAME_JZPAJ_TW,
	GAME_QQ_CAR,
	GAME_QQ_CAR_TW,
	GAME_BRAWLSTARS,
	GAME_CLASHROYALE_H2,
	GAME_CLASHROYALE,
	GAME_DWRG_H2,
	GAME_DWRG,
	GAME_DWRG_TW,
	GAME_MRZH_H2,
	GAME_MRZH,
	GAME_MRZH_TW,
};

static int enable_to_user;
static int op_sla_rtt_detect;
static int op_sla_link_turbo;
static int op_sla_enable;
static int op_sla_debug;
static int fw_set_game_mark;
static int op_sla_calc_speed;
static int op_sla_def_net;    //WLAN->0 CELL->1
static int game_start_state;
static int game_rtt_wan_detect_flag;
static int rtt_rear;
static int rtt_queue[MAX_RTT_RECORD_NUM];
static int rtt_record_num = MAX_RTT_RECORD_NUM;

static bool sla_switch_enable;
static bool sla_screen_on;
static bool need_pop_window;

static u32 op_sla_pid;
static struct sock *op_sla_sock;
static struct timer_list sla_timer;

static struct timeval last_speed_tv;
static struct timeval last_weight_tv;
static struct timeval last_minute_speed_tv;
static struct timeval last_enable_cellular_tv;
static struct timeval disable_cellular_tv;
static struct timeval calc_wlan_rtt_tv;
static struct timeval last_enable_to_user_tv;
static struct timeval last_calc_small_speed_tv;

static struct op_game_online game_online_info;
static struct op_white_app_info white_app_list;
static struct op_game_app_info op_sla_game_app_list;
static struct op_dev_info op_sla_info[IFACE_NUM];
static struct op_speed_calc op_speed_info[IFACE_NUM];
static struct op_syn_retran_statistic syn_retran_statistic;

static DEFINE_MUTEX(sla_netlink_mutex);
static struct ctl_table_header *op_sla_table_hrd;

static rwlock_t sla_lock;
static rwlock_t sla_rtt_lock;
static rwlock_t sla_game_lock;
static rwlock_t sla_game_rx_lock;

#define sla_read_lock()			  read_lock_bh(&sla_lock)
#define sla_read_unlock()		  read_unlock_bh(&sla_lock)
#define sla_write_lock()		  write_lock_bh(&sla_lock)
#define sla_write_unlock()		  write_unlock_bh(&sla_lock)

#define sla_rtt_write_lock()		  write_lock_bh(&sla_rtt_lock)
#define sla_rtt_write_unlock()		  write_unlock_bh(&sla_rtt_lock)

#define sla_game_write_lock()		  write_lock_bh(&sla_game_lock)
#define sla_game_write_unlock()		  write_unlock_bh(&sla_game_lock)

#define sla_game_rx_error_write_lock()	  write_lock_bh(&sla_game_rx_lock)
#define sla_game_rx_error_write_unlock()  write_unlock_bh(&sla_game_rx_lock)

static struct op_sla_params_info sla_params_info = {
	.sla_speed = 200,
	.cell_speed = MAX_CELLULAR_SPEED,
	.wlan_speed = MAX_WLAN_SPEED,
	.wlan_little_score_speed = LOW_RSSI_MAX_WLAN_SPEED,
	.sla_rtt = 200,
	.wzry_rtt = 200,
	.cjzc_rtt = 300,
	.pubg_rtt = 300,
	.qqcar_rtt = 300,
	.wlan_bad_score = WLAN_SCORE_BAD,
	.wlan_good_score = WLAN_SCORE_GOOD,
};

static void reset_sla_game_app_rx_error(int game_type)
{
	op_sla_game_app_list.special_rx_error_count[game_type] = 0;
	op_sla_game_app_list.special_rx_count[game_type] = 0;
}

static void rttQueueEnqueue(int data)
{
	if (rtt_rear == rtt_record_num) {
		if (op_sla_debug)
			pr_info("[op_sla] %s: Rtt queue is full\n", __func__);
		return;
	}
	rtt_queue[rtt_rear] = data;
	rtt_rear++;
}

static void rttQueueDequeue(void)
{
	int i;

	if (rtt_rear == 0) {
		if (op_sla_debug)
			pr_info("[op_sla] %s: Rtt queue is empty\n", __func__);
		return;
	}
	for (i = 0; i < rtt_rear - 1; i++)
		rtt_queue[i] = rtt_queue[i + 1];
	rtt_rear--;
}

static int averageRttQueue(void)
{
	int sum = 0;
	int i;

	for (i = 0; i < rtt_rear; i++)
		sum += rtt_queue[i] * (i + 1) / 10;
	return sum;
}

static void init_rtt_queue_info(void)
{
	rtt_rear = 0;
	memset(rtt_queue, 0, sizeof(rtt_queue));
}

static void reset_sla_game_app_rtt(int game_type)
{
	init_rtt_queue_info();
	op_sla_game_app_list.rtt[game_type] = 0;
	op_sla_game_app_list.rtt_250_num[game_type] = 0;
	op_sla_game_app_list.rtt_200_num[game_type] = 0;
	op_sla_game_app_list.rtt_150_num[game_type] = 0;
	op_sla_game_app_list.rtt_normal_num[game_type] = 0;
}

//send to user space
static int get_app_type(struct nf_conn *ct)
{
	if (ct->op_app_type >= GAME_BASE &&
	    ct->op_app_type < (GAME_BASE + GAME_NUM))
		return GAME_TYPE;
	else if (ct->op_app_type >= WHITE_APP_BASE)
		return WHITE_APP_TYPE;
	else
		return INIT_APP_TYPE;
}

static bool get_ct_cell_quality(struct sk_buff *skb, int game_type)
{
	int score_base = 0;

	if (op_sla_game_app_list.mark[game_type] == CELLULAR_MARK)
		score_base = 10;

	if (game_type == GAME_WZRY) {
		return op_sla_info[CELLULAR_INDEX].cell_score >=
			(CELL_SCORE_BAD - score_base);
	} else if (game_type == GAME_CJZC || game_type == GAME_PUBG ||
		   game_type == GAME_PUBG_TW || game_type == GAME_QQ_CAR ||
		   game_type == GAME_QQ_CAR_TW) {
		return op_sla_info[CELLULAR_INDEX].cell_score >=
			(-110 - score_base);
	} else {
		return op_sla_info[CELLULAR_INDEX].cell_score >=
			(CELL_SCORE_BAD - score_base);
	}
}

static int op_sla_send_to_user(int msg_type, char *payload, int payload_len)
{
	int ret = -1;
	struct sk_buff *skbuff;
	struct nlmsghdr *nlh;

	if (!op_sla_pid) {
		if (op_sla_debug)
			pr_info("[op_sla] %s: op_sla_pid == 0!!\n", __func__);
		return ret;
	}

	//allocate new buffer cache
	skbuff = alloc_skb(NLMSG_SPACE(payload_len), GFP_ATOMIC);
	if (!skbuff) {
		if (op_sla_debug)
			pr_info("[op_sla] %s: skbuff alloc_skb failed\n",
				__func__);
		return ret;
	}

	//fill in the data structure
	nlh = nlmsg_put(skbuff, 0, 0, msg_type, NLMSG_ALIGN(payload_len), 0);
	if (!nlh) {
		if (op_sla_debug)
			pr_info("[op_sla] %s: nlmsg_put failaure\n", __func__);
		nlmsg_free(skbuff);
		return ret;
	}

	//compute nlmsg length
	nlh->nlmsg_len = NLMSG_HDRLEN + NLMSG_ALIGN(payload_len);

	if (payload)
		memcpy((char *)NLMSG_DATA(nlh), payload, payload_len);

	//set control field,sender's pid
#if (KERNEL_VERSION(3, 7, 0) > LINUX_VERSION_CODE)
	NETLINK_CB(skbuff).pid = 0;
#else
	NETLINK_CB(skbuff).portid = 0;
#endif
	NETLINK_CB(skbuff).dst_group = 0;

	//send data
	ret = netlink_unicast(op_sla_sock, skbuff, op_sla_pid, MSG_DONTWAIT);
	if (ret < 0) {
		if (op_sla_debug) {
			pr_info("[op_sla] %s: can not unicast skbuff,ret = %d\n",
				__func__, ret);
		}
		return 1;
	}
	return 0;
}

static void prepare_enable_sla(void)
{
	if (op_sla_debug) {
		pr_info("[op_sla] %s: wlan need_up:%d cell need_up:%d\n",
			__func__,
			op_sla_info[WLAN_INDEX].need_up,
			op_sla_info[CELLULAR_INDEX].need_up);
	}
	enable_to_user = 1;
	if (op_sla_info[WLAN_INDEX].need_up)
		op_sla_info[WLAN_INDEX].if_up = 1;

	do_gettimeofday(&last_enable_to_user_tv);

	if (op_sla_info[CELLULAR_INDEX].need_up) {
		op_sla_info[CELLULAR_INDEX].if_up = 1;
		op_sla_info[CELLULAR_INDEX].need_up = false;
	}
	op_sla_send_to_user(SLA_ENABLE, NULL, 0);
}

static void back_iface_speed(void)
{
	int i = 0;
	int rtt_num = 2;

	if (op_sla_info[WLAN_INDEX].wlan_score <=
	    sla_params_info.wlan_bad_score)
		rtt_num = 1;

	sla_rtt_write_lock();
	for (i = 0; i < IFACE_NUM; i++) {
		if (op_sla_info[i].if_up &&
		    op_sla_info[i].sla_rtt_num >= rtt_num) {
			if (op_sla_info[i].download_flag != DOWN_LOAD_FLAG) {
				op_sla_info[i].sla_avg_rtt =
					op_sla_info[i].sla_sum_rtt /
					op_sla_info[i].sla_rtt_num;
			}

			if (op_sla_debug) {
				pr_info("[op_sla] %s: sla_rtt_num:%d sum:%d\n",
					__func__, op_sla_info[i].sla_rtt_num,
					op_sla_info[i].sla_sum_rtt);
				pr_info("[op_sla] %s: avg rtt[%d]:%d\n",
					__func__, i,
					op_sla_info[i].sla_avg_rtt);
			}

			op_sla_info[i].sla_rtt_num = 0;
			op_sla_info[i].sla_sum_rtt = 0;

			if (op_sla_info[i].sla_avg_rtt >= BACK_OFF_RTT_2) {
				op_sla_info[i].congestion_flag =
					CONGESTION_LEVEL_HIGH;
				op_sla_info[i].max_speed /= 2;
			} else if (op_sla_info[i].sla_avg_rtt >=
				   BACK_OFF_RTT_1) {
				op_sla_info[i].congestion_flag =
					CONGESTION_LEVEL_MIDDLE;
				op_sla_info[i].max_speed /= 2;
			} else {
				if (WEIGHT_STATE_NORMAL ==
				    op_sla_info[i].weight_state) {
					op_sla_info[i].congestion_flag =
						CONGESTION_LEVEL_NORMAL;
				}
			}
		}
	}
	sla_rtt_write_unlock();
}

static void reset_wlan_info(void)
{
	struct timeval tv;

	if (op_sla_info[WLAN_INDEX].if_up) {
		do_gettimeofday(&tv);
		last_minute_speed_tv = tv;

		op_sla_info[WLAN_INDEX].sum_rtt = 0;
		op_sla_info[WLAN_INDEX].avg_rtt = 0;
		op_sla_info[WLAN_INDEX].rtt_index = 0;
		op_sla_info[WLAN_INDEX].sla_avg_rtt = 0;
		op_sla_info[WLAN_INDEX].sla_rtt_num = 0;
		op_sla_info[WLAN_INDEX].sla_sum_rtt = 0;
		op_sla_info[WLAN_INDEX].syn_retran = 0;
		op_sla_info[WLAN_INDEX].max_speed /= 2;
		op_sla_info[WLAN_INDEX].left_speed = 0;
		op_sla_info[WLAN_INDEX].current_speed = 0;
		op_sla_info[WLAN_INDEX].minute_speed = MINUTE_LITTE_RATE;
		op_sla_info[WLAN_INDEX].congestion_flag =
			CONGESTION_LEVEL_NORMAL;
	}
}

static int enable_op_sla_module(void)
{
	struct timeval tv;

	if (op_sla_debug) {
		pr_info("[op_sla] %s: wlan-if_up:%d cell-if_up:%d link_turbo:%d\n",
			__func__, op_sla_info[WLAN_INDEX].if_up,
			op_sla_info[CELLULAR_INDEX].if_up, op_sla_link_turbo);
	}

	sla_write_lock();

	if ((op_sla_info[WLAN_INDEX].if_up &&
	     op_sla_info[CELLULAR_INDEX].if_up) || op_sla_link_turbo) {
		op_sla_enable = 1;
		do_gettimeofday(&tv);
		last_weight_tv = tv;
		last_enable_cellular_tv = tv;
		op_sla_info[WLAN_INDEX].weight = 30;
		op_sla_info[CELLULAR_INDEX].weight = 100;
		op_sla_send_to_user(SLA_ENABLED, NULL, 0);
	}

	sla_write_unlock();
	return 0;
}

static void calc_rtt_by_dev_index(int index, int tmp_rtt)
{
	// Do not calc rtt when the screen
	// is off which may make the rtt too big
	if (!sla_screen_on)
		return;

	if (tmp_rtt < 30)
		return;

	sla_rtt_write_lock();

	op_sla_info[index].rtt_index++;
	if (tmp_rtt > MAX_RTT)
		tmp_rtt = MAX_RTT;

	op_sla_info[index].sum_rtt += tmp_rtt;
	sla_rtt_write_unlock();
}

static int find_iface_index_by_mark(__u32 mark)
{
	int i;
	int index = -1;

	for (i = 0; i < IFACE_NUM; i++) {
		if (op_sla_info[i].if_up && mark == op_sla_info[i].mark)
			return i;
	}
	return index;
}

static int calc_retran_syn_rtt(struct sk_buff *skb, struct nf_conn *ct)
{
	int index = -1;
	int ret = SLA_SKB_CONTINUE;
	int tmp_mark = ct->mark & MARK_MASK;
	int rtt_mark = ct->mark & RTT_MASK;

	if (rtt_mark & RTT_MARK) {
		skb->mark = ct->mark;
		return SLA_SKB_MARKED;
	}

	index = find_iface_index_by_mark(tmp_mark);

	if (index != -1) {
		calc_rtt_by_dev_index(index, SYN_RETRAN_RTT);

		op_sla_info[index].syn_retran++;
		syn_retran_statistic.syn_retran_num++;

		ct->mark |= RTT_MARK;
		skb->mark = ct->mark;

		ret = SLA_SKB_MARKED;
	}
	syn_retran_statistic.syn_total_num++;
	return ret;
}

static int get_syn_retran_iface_index(struct nf_conn *ct)
{
	int index = -1;
	int tmp_mark = ct->mark & MARK_MASK;

	if (tmp_mark == WLAN_MARK) {
		if (op_sla_info[CELLULAR_INDEX].if_up &&
		    op_sla_info[WLAN_INDEX].congestion_flag ==
		    CONGESTION_LEVEL_HIGH &&
		    op_sla_info[CELLULAR_INDEX].congestion_flag ==
		    CONGESTION_LEVEL_NORMAL) {
			index = WLAN_INDEX;
		}
	} else if (tmp_mark == CELLULAR_MARK) {
		if (op_sla_info[WLAN_INDEX].if_up &&
		    op_sla_info[WLAN_INDEX].max_speed >
		    CALC_WEIGHT_MIN_SPEED_3 &&
		    op_sla_info[WLAN_INDEX].congestion_flag ==
		    CONGESTION_LEVEL_NORMAL &&
		    op_sla_info[CELLULAR_INDEX].congestion_flag ==
		    CONGESTION_LEVEL_HIGH) {
			index = CELLULAR_INDEX;
		}
	}
	return index;
}

// icsk_syn_retries
static int syn_retransmits_packet_do_specail(struct sock *sk,
					     struct nf_conn *ct,
					     struct sk_buff *skb)
{
	int index = -1;
	int ret = SLA_SKB_CONTINUE;
	u32 tmp_retran_mark = sk->op_sla_mark & RETRAN_MASK;
	struct iphdr *iph;
	struct tcphdr *th = NULL;
	struct inet_connection_sock *icsk = inet_csk(sk);

	iph = ip_hdr(skb);
	if (iph && iph->protocol == IPPROTO_TCP) {
		th = tcp_hdr(skb);

		//Only statistic syn retran packet,
		//sometimes some rst packets also will be here
		if (th && th->syn && !th->rst && !th->ack && !th->fin) {
			ret = calc_retran_syn_rtt(skb, ct);

			if (RETRAN_SECOND_MARK & tmp_retran_mark) {
				skb->mark = ct->mark;
				return SLA_SKB_MARKED;
			}

			if (!nf_ct_is_dying(ct) && nf_ct_is_confirmed(ct)) {
				index = get_syn_retran_iface_index(ct);
				if (index != -1) {
					ct->mark = 0x0;
					//reset the tcp rto, so that can be
					//faster to retrans to another dev
					icsk->icsk_rto = TCP_RTO_MIN;
					sk->op_sla_mark |= RETRAN_MARK;

					//Del the ct information, so that the
					//syn packet can be send to network
					//successfully later.
					//lookup nf_nat_ipv4_fn()
					nf_ct_kill(ct);
					ret = SLA_SKB_REMARK;
				}
			}
		} else {
			ret = SLA_SKB_ACCEPT;
		}
	}

	return ret;
}

static int mark_retransmits_syn_skb(struct sock *sk, struct nf_conn *ct)
{
	u32 tmp_mark = sk->op_sla_mark & MARK_MASK;
	u32 tmp_retran_mark = sk->op_sla_mark & RETRAN_MASK;

	if (RETRAN_MARK & tmp_retran_mark) {
		if (tmp_mark == WLAN_MARK)
			return (CELLULAR_MARK | RETRAN_SECOND_MARK);
		else if (tmp_mark == CELLULAR_MARK)
			return (WLAN_MARK | RETRAN_SECOND_MARK);
	}
	return 0;
}

static void is_http_get(struct nf_conn *ct, struct sk_buff *skb,
			struct tcphdr *tcph, int header_len)
{
	u32 *payload = NULL;

	payload = (u32 *)(skb->data + header_len);
	if (ct->op_http_flag == 0 && ntohs(tcph->dest) == 80) {
		if (*payload == 0x20544547) {//http get
			ct->op_http_flag = 1;
			ct->op_skb_count = 1;
		}
	}
}

static struct tcphdr *is_valid_http_packet(struct sk_buff *skb,
					   int *header_len)
{
	int datalen = 0;
	int tmp_len = 0;
	struct tcphdr *tcph = NULL;
	struct iphdr *iph;

	iph = ip_hdr(skb);
	if (iph && iph->protocol == IPPROTO_TCP) {
		tcph = tcp_hdr(skb);
		datalen = ntohs(iph->tot_len);
		tmp_len = iph->ihl * 4 + tcph->doff * 4;

		if ((datalen - tmp_len) > 64) {
			*header_len = tmp_len;
			return tcph;
		}
	}
	return NULL;
}

static void reset_dev_info_syn_retran(struct nf_conn *ct,
				      struct sk_buff *skb)
{
	int index = -1;
	u32 tmp_mark;
	struct tcphdr *tcph;
	int header_len = 0;
	int syn_retran_num = 0;

	tmp_mark = ct->mark & MARK_MASK;
	index = find_iface_index_by_mark(tmp_mark);

	if (index != -1) {
		syn_retran_num = op_sla_info[index].syn_retran;
		tcph = is_valid_http_packet(skb, &header_len);
		if (syn_retran_num && tcph)
			op_sla_info[index].syn_retran = 0;
	}
}

static u32 get_skb_mark_by_weight(void)
{
	u32 sla_random = prandom_u32() & 0x7FFFFFFF;

	if (op_sla_link_turbo)
		op_sla_info[WLAN_INDEX].weight = 50;

	if (op_sla_debug) {
		pr_info("[op_sla] %s: wlan weight:%d\n", __func__,
			op_sla_info[WLAN_INDEX].weight);
	}

	// 0x147AE15 = 0x7FFFFFFF/100 + 1; We let the weight * 100 to void
	// decimal point operation at linux kernel
	if (sla_random < (0x147AE15 * op_sla_info[WLAN_INDEX].weight))
		return op_sla_info[WLAN_INDEX].mark;
	return op_sla_info[CELLULAR_INDEX].mark;
}

//maybe this method can let calc dev speed more fairness
static int set_skb_mark_by_default(void)
{
	int mark_flag = 0;

	if (op_sla_info[CELLULAR_INDEX].if_up) {
		mark_flag = 1;
		return CELLULAR_MARK & MARK_MASK;
	}
	return WLAN_MARK & MARK_MASK;
}

static void reset_op_sla_calc_speed(struct timeval tv)
{
	int time_interval = tv.tv_sec - last_calc_small_speed_tv.tv_sec;

	if (time_interval >= 60 &&
	    op_speed_info[WLAN_INDEX].speed_done) {
		op_sla_calc_speed = 0;
		op_speed_info[WLAN_INDEX].speed_done = 0;
	}
}

static int wlan_get_speed_prepare(struct sk_buff *skb)
{
	int header_len = 0;
	struct tcphdr *tcph = NULL;
	struct nf_conn *ct = NULL;
	enum ip_conntrack_info ctinfo;

	ct = nf_ct_get(skb, &ctinfo);

	if (!ct)
		return NF_ACCEPT;

	if (ctinfo == IP_CT_ESTABLISHED) {
		tcph = is_valid_http_packet(skb, &header_len);
		if (tcph)
			is_http_get(ct, skb, tcph, header_len);
	}
	return NF_ACCEPT;
}

static int get_wlan_syn_retran(struct sk_buff *skb)
{
	int tmp_mark;
	int rtt_mark;
	struct iphdr *iph;
	struct sock *sk = NULL;
	struct nf_conn *ct = NULL;
	struct tcphdr *th = NULL;
	enum ip_conntrack_info ctinfo;

	ct = nf_ct_get(skb, &ctinfo);

	if (!ct)
		return NF_ACCEPT;

	iph = ip_hdr(skb);
	if (ctinfo == IP_CT_NEW &&
	    iph && iph->protocol == IPPROTO_TCP) {
		th = tcp_hdr(skb);

		// Only statistic syn retran packet,
		// sometimes some rst packet also will be here
		if (th && th->syn && !th->rst && !th->ack && !th->fin) {
			rtt_mark = ct->mark & RTT_MASK;
			tmp_mark = ct->mark & MARK_MASK;

			if (rtt_mark & RTT_MARK)
				return NF_ACCEPT;

			if (tmp_mark != WLAN_MARK) {
				ct->mark = WLAN_MARK;

				sk = skb_to_full_sk(skb);
				if (sk)
					sk->op_sla_mark = WLAN_MARK;

				syn_retran_statistic.syn_total_num++;

				return NF_ACCEPT;
			}

			syn_retran_statistic.syn_retran_num++;
			calc_rtt_by_dev_index(WLAN_INDEX, SYN_RETRAN_RTT);

			ct->mark |= RTT_MARK;
		}
	}
	return NF_ACCEPT;
}

static void rx_interval_error_estimator(int game_type, int time_error,
					struct nf_conn *ct)
{
	int dropnum = 0;
	int gamethreshold = 0;

	if (game_type == GAME_WZRY) {
		dropnum = 5;
		gamethreshold = 300;
	} else if (game_type == GAME_QQ_CAR) {
		dropnum = 2;
		gamethreshold = 1000;
	}
	if (dropnum != 0 &&
	    op_sla_game_app_list.special_rx_count[game_type] >= dropnum &&
	    (gamethreshold != 0 && time_error >= gamethreshold))
		op_sla_game_app_list.special_rx_error_count[game_type]++;
	else if (dropnum != 0 &&
		 op_sla_game_app_list.special_rx_count[game_type]
		 >= dropnum &&
		 op_sla_game_app_list.special_rx_error_count[game_type])
		op_sla_game_app_list.special_rx_error_count[game_type]--;

	if (op_sla_debug) {
		pr_info("[op_sla] %s: time_error:%d error_count:%d\n",
			__func__, time_error,
			op_sla_game_app_list.special_rx_error_count[game_type]);
	}
	op_sla_game_app_list.special_rx_count[game_type]++;
}

static void game_rtt_estimator(int game_type, int rtt, struct nf_conn *ct)
{
	int averagertt = 0;
	int rttdropcount = op_sla_game_app_list.rtt_250_num[game_type]
			+ op_sla_game_app_list.rtt_200_num[game_type]
			+ op_sla_game_app_list.rtt_150_num[game_type]
			+ op_sla_game_app_list.rtt_normal_num[game_type];
	int app_type = get_app_type(ct);
	bool game_rtt_detect_lag = false;

	if (app_type == GAME_TYPE) {
		if (rttdropcount >= (rtt_record_num >> 1)) {
			if (rtt_rear == rtt_record_num) {
				rttQueueDequeue();
				rttQueueEnqueue(rtt);
				averagertt = averageRttQueue();
			} else {
				rttQueueEnqueue(rtt);
			}
		}

		if (op_sla_debug) {
			pr_info("[op_sla] %s: rtt:%d averagertt:%d\n",
				__func__, rtt, averagertt);
		}
		if (game_type == GAME_WZRY) {
			game_rtt_detect_lag =
				(abs(ct->op_game_time_interval - 5000) >= 50);
			//if game rtt not regular and last game rtt
			//over 300 ms
			if (game_rtt_detect_lag &&
			    rtt_rear == rtt_record_num &&
			    rtt == MAX_GAME_RTT) {
				averagertt = MAX_GAME_RTT;
				if (op_sla_debug) {
					pr_info("[op_sla] %s: detect max game rtt\n",
						__func__);
				}
			//if game rtt continue over 200ms and current
			//rtt bigger than before
			} else if (rtt_rear == rtt_record_num &&
				   (rtt_queue[rtt_rear - 2] >=
				    sla_params_info.wzry_rtt &&
				    rtt_queue[rtt_rear - 1] >=
				    sla_params_info.wzry_rtt)) {
				averagertt = MAX_GAME_RTT;
				if (op_sla_debug) {
					pr_info("[op_sla] %s: continue bad\n",
						__func__);
				}
			} else if (ct->op_game_lost_count >= 1 &&
				   rtt == MAX_GAME_RTT) {
				averagertt = MAX_GAME_RTT;
				if (op_sla_debug) {
					pr_info("[op_sla] %s: detect game rtt lost\n",
						__func__);
				}
			}
		} else if (game_type == GAME_PUBG ||
			   game_type == GAME_PUBG_TW ||
			   game_type == GAME_AOV ||
			   game_type == GAME_QQ_CAR_TW) {
			if (ct->op_game_lost_count >= 1 &&
			    rtt == MAX_GAME_RTT &&
			    game_rtt_wan_detect_flag) {
				averagertt = MAX_GAME_RTT;
				if (op_sla_debug) {
					pr_info("[op_sla] %s: detect game rtt lost\n",
						__func__);
				}
			}
		} else if (game_type == GAME_CJZC) {
			if (ct->op_game_lost_count >= 3 &&
			    rtt == MAX_GAME_RTT) {
				averagertt = MAX_GAME_RTT;
				if (op_sla_debug) {
					pr_info("[op_sla] %s: detect game rtt lost\n",
						__func__);
				}
			}
		}

		op_sla_game_app_list.rtt[game_type] = averagertt;

		if (rtt >= 250)
			op_sla_game_app_list.rtt_250_num[game_type]++;
		else if (rtt >= 200)
			op_sla_game_app_list.rtt_200_num[game_type]++;
		else if (rtt >= 150)
			op_sla_game_app_list.rtt_150_num[game_type]++;
		else
			op_sla_game_app_list.rtt_normal_num[game_type]++;
	}
}

static void game_app_switch_network(struct nf_conn *ct, struct sk_buff *skb)
{
	int game_type = ct->op_app_type;
	int game_rtt = op_sla_game_app_list.rtt[game_type];
	u32 time_now = ktime_get_ns() / 1000000;
	int max_rtt = sla_params_info.sla_rtt;
	int game_bp_info[4];
	bool wlan_bad = false;
	bool cell_quality_good = get_ct_cell_quality(skb, game_type);
	u32 game_switch_interval = time_now -
		op_sla_game_app_list.switch_time[game_type];

	if (!op_sla_enable)
		return;

	if (!game_start_state)
		return;

	if (game_type == GAME_WZRY) {
		max_rtt = sla_params_info.wzry_rtt;
		if (rtt_rear == 4 && ct->op_game_lost_count == 0 &&
		    rtt_queue[0] == MAX_GAME_RTT &&
		    rtt_queue[1] == MAX_GAME_RTT &&
		    rtt_queue[2] == MAX_GAME_RTT &&
		    rtt_queue[3] == MAX_GAME_RTT) {
			game_rtt = 0;
		}
	} else if (game_type == GAME_CJZC) {
		max_rtt = sla_params_info.cjzc_rtt;
	} else if (game_type == GAME_PUBG || game_type == GAME_PUBG_TW) {
		max_rtt = sla_params_info.pubg_rtt;
	} else if (game_type == GAME_QQ_CAR_TW) {
		max_rtt = sla_params_info.qqcar_rtt;
	}

	if (op_sla_info[WLAN_INDEX].wlan_score_bad_count >= WLAN_SCORE_BAD_NUM)
		wlan_bad = true;

	if (op_sla_debug) {
		pr_info("[op_sla] %s: cell_quality_good:%d wlan_bad:%d game_rtt:%d\n",
			__func__, cell_quality_good, wlan_bad, game_rtt);
		pr_info("[op_sla] %s: special_rx_error_count:%d game mark:%d time:%d\n",
			__func__,
			op_sla_game_app_list.special_rx_error_count[game_type],
			op_sla_game_app_list.mark[game_type],
			(time_now -
			 op_sla_game_app_list.switch_time[game_type]));
		pr_info("[op_sla] %s: wlan valid:%d cell valid:%d\n", __func__,
			op_sla_info[WLAN_INDEX].netlink_valid,
			op_sla_info[CELLULAR_INDEX].netlink_valid);
	}

	if (op_sla_game_app_list.switch_count[game_type] > 1 &&
	    op_sla_game_app_list.repeat_switch_time[game_type] != 0 &&
	    (time_now - op_sla_game_app_list.repeat_switch_time[game_type]) <
	     PINGPONG_AVOID_TIME) {
		if (op_sla_debug) {
			pr_info("[op_sla] %s: avoid ping-pong switch\n",
				__func__);
		}
		return;
	}

	if (((cell_quality_good && op_sla_info[CELLULAR_INDEX].netlink_valid &&
	      ((game_rtt != 0 && game_rtt >= max_rtt) ||
	       op_sla_game_app_list.special_rx_error_count[game_type] >= 2) &&
	       op_sla_game_app_list.mark[game_type] == WLAN_MARK) &&
	       (!op_sla_game_app_list.switch_time[game_type] ||
	       game_switch_interval > 30000)) || fw_set_game_mark == 1) {
		fw_set_game_mark = -1;
		if (op_sla_debug) {
			pr_info("[op_sla] %s: game switch to cellular...\n",
				__func__);
		}
		reset_sla_game_app_rx_error(game_type);
		reset_sla_game_app_rtt(game_type);
		op_sla_game_app_list.switch_count[game_type]++;
		if (op_sla_game_app_list.switch_count[game_type] > 1 &&
		    game_switch_interval < GAME_LINK_SWITCH_TIME) {
			op_sla_game_app_list.repeat_switch_time[game_type] =
				time_now;
		}
		op_sla_game_app_list.switch_time[game_type] = time_now;
		op_sla_game_app_list.mark[game_type] = CELLULAR_MARK;

		memset(game_bp_info, 0x0, sizeof(game_bp_info));
		game_bp_info[0] = game_type;
		game_bp_info[1] = CELLULAR_MARK;
		game_bp_info[2] = wlan_bad;
		game_bp_info[3] = cell_quality_good;
		op_sla_send_to_user(SLA_SWITCH_GAME_NETWORK,
				    (char *)game_bp_info,
				    sizeof(game_bp_info));
		return;
	}

	if (((!wlan_bad && op_sla_info[WLAN_INDEX].netlink_valid &&
	      ((game_rtt != 0 && game_rtt >= max_rtt) ||
	       op_sla_game_app_list.special_rx_error_count[game_type] >= 2) &&
	       op_sla_game_app_list.mark[game_type] == CELLULAR_MARK) &&
	       (!op_sla_game_app_list.switch_time[game_type] ||
	       game_switch_interval > 30000)) || fw_set_game_mark == 0) {
		fw_set_game_mark = -1;
		if (op_sla_debug) {
			pr_info("[op_sla] %s: game switch to wlan...\n",
				__func__);
		}
		reset_sla_game_app_rx_error(game_type);
		reset_sla_game_app_rtt(game_type);
		op_sla_game_app_list.switch_count[game_type]++;
		if (game_switch_interval < GAME_LINK_SWITCH_TIME) {
			op_sla_game_app_list.repeat_switch_time[game_type] =
				time_now;
		}
		op_sla_game_app_list.switch_time[game_type] = time_now;
		op_sla_game_app_list.mark[game_type] = WLAN_MARK;

		memset(game_bp_info, 0x0, sizeof(game_bp_info));
		game_bp_info[0] = game_type;
		game_bp_info[1] = WLAN_MARK;
		game_bp_info[2] = wlan_bad;
		game_bp_info[3] = cell_quality_good;
		op_sla_send_to_user(SLA_SWITCH_GAME_NETWORK,
				    (char *)game_bp_info,
				    sizeof(game_bp_info));
		return;
	}
}

static void set_game_rtt_stream_up_info(struct nf_conn *ct, s64 now,
					u32 game_type)
{
	int game_rtt;
	int game_lost_count_threshold;

	if (game_type == GAME_WZRY) {
		if (op_sla_game_app_list.mark[game_type] == CELLULAR_MARK)
			game_lost_count_threshold = 2;
		else
			game_lost_count_threshold = 1;
	} else if (game_type == GAME_PUBG || game_type == GAME_PUBG_TW ||
		game_type == GAME_AOV || game_type == GAME_QQ_CAR_TW) {
		game_lost_count_threshold = 1;
	} else {
		game_lost_count_threshold = 3;
	}

	if (!ct->op_game_timestamp && !game_rtt_wan_detect_flag) {
		ct->op_game_timestamp = now;
		if (game_type == GAME_PUBG || game_type == GAME_PUBG_TW ||
		    game_type == GAME_QQ_CAR_TW) {
			ct->op_game_time_interval = 5000;
		} else if (game_type == GAME_AOV) {
			ct->op_game_time_interval = 2000;
		} else if (game_type == GAME_CJZC) {
			ct->op_game_time_interval = 1000;
		} else {
			ct->op_game_time_interval =
				now - ct->op_game_last_timestamp;
		}
		ct->op_game_last_timestamp = now;
		ct->op_game_lost_count = 0;
		if (ct->op_game_time_interval >= 10000)
			ct->op_game_timestamp = 0;
		if ((game_type == GAME_PUBG || game_type == GAME_PUBG_TW ||
		     game_type == GAME_AOV || game_type == GAME_QQ_CAR_TW) &&
		     !game_rtt_wan_detect_flag) {
			game_rtt_wan_detect_flag = 1;
			return;
		}
	} else {
		ct->op_game_timestamp = now;
		ct->op_game_last_timestamp = now;
		ct->op_game_lost_count++;
		if (op_sla_debug) {
			pr_info("[op_sla] %s: lost game detect skb count:%d\n",
				__func__, ct->op_game_lost_count);
		}
		if (op_sla_enable &&
		    ct->op_game_lost_count >= game_lost_count_threshold &&
		    (ct->op_game_time_interval > 300 ||
		     game_rtt_wan_detect_flag)) {
			game_rtt = MAX_GAME_RTT;
			if (op_sla_debug) {
				pr_info("[op_sla] %s: lost detect skb, game_type:%d\n",
					__func__, game_type);
				pr_info("[op_sla] %s: last game rtt:%d\n",
					__func__,
					op_sla_game_app_list.rtt[game_type]);
			}
			sla_game_write_lock();
			game_rtt_estimator(game_type, game_rtt, ct);
			sla_game_write_unlock();
			game_rtt_wan_detect_flag = 0;
		}
	}
}

static void detect_game_tx_stream(struct nf_conn *ct, struct sk_buff *skb,
				  enum ip_conntrack_info ctinfo)
{
	int game_type = ct->op_app_type;
	s64 time_now = ktime_get_ns() / 1000000;
	int specialrxthreshold = 0;
	int lastspecialrxtiming = 0;
	int datastallthreshold = 0;
	int datastalltimer = 0;

	if (op_sla_debug) {
		pr_info("[op_sla] %s: time_now:%llu\n",
			__func__, time_now);
		pr_info("[op_sla] %s: op_game_special_rx_pkt_last_timestamp:%llu\n",
			__func__, ct->op_game_special_rx_pkt_last_timestamp);
		pr_info("[op_sla] %s: op_game_rx_normal_time_record:%llu\n",
			__func__, ct->op_game_rx_normal_time_record);
	}

	if (game_type == GAME_QQ_CAR &&
	    ct->op_game_special_rx_pkt_last_timestamp) {
		ct->op_game_special_rx_pkt_interval = 10000;
		specialrxthreshold =
			ct->op_game_special_rx_pkt_interval * 1.2;
		datastallthreshold =
			ct->op_game_special_rx_pkt_interval / 2;
		datastalltimer = (int)(time_now -
			ct->op_game_rx_normal_time_record);
		lastspecialrxtiming = (int)(time_now -
			ct->op_game_special_rx_pkt_last_timestamp);
		if (op_sla_enable &&
		    lastspecialrxtiming >= specialrxthreshold &&
		    datastalltimer >= datastallthreshold) {
			if (op_sla_debug) {
				pr_info("[op_sla] %s: lastspecialrxtiming:%d\n",
					__func__, lastspecialrxtiming);
				pr_info("[op_sla] %s: datastalltimer:%d\n",
					__func__, datastalltimer);
			}
			sla_game_rx_error_write_lock();
			rx_interval_error_estimator(game_type,
						    datastalltimer,
						    ct);
			sla_game_rx_error_write_unlock();
		}
	}
}

static void detect_game_rtt_stream(struct nf_conn *ct, struct sk_buff *skb,
				   enum ip_conntrack_info ctinfo)
{
	int same_count_max = 10;
	int up_max_count = 100;
	s64 time_now = ktime_get_ns() / 1000000;
	s64 time_interval = time_now - ct->op_game_timestamp;
	int game_type = ct->op_app_type;

	if (game_type == GAME_WZRY) {
		if (skb->len == 47)
			same_count_max = 3;
		else
			return;
	} else if (game_type == GAME_CJZC) {
		same_count_max = 6;
	} else if ((game_type == GAME_PUBG || game_type == GAME_PUBG_TW ||
		    game_type == GAME_AOV || game_type == GAME_QQ_CAR_TW) &&
		    skb->len == 33) {
		ct->op_game_detect_status = GAME_RTT_DETECTED_STREAM;
		ct->op_game_up_count++;
	}

	if (op_sla_debug) {
		pr_info("[op_sla] %s: game type:%d timestamp:%llu inter:%u\n",
			__func__, game_type, ct->op_game_timestamp,
			ct->op_game_time_interval);
		pr_info("[op_sla] %s: src port:%d ct state:%d up count:%d game_status:%d\n",
			__func__,
			ntohs(udp_hdr(skb)->source), XT_STATE_BIT(ctinfo),
			ct->op_game_up_count, ct->op_game_detect_status);
		pr_info("[op_sla] %s: skb len:%d, game_rtt_wan_detect_flag:%d\n",
			__func__, skb->len, game_rtt_wan_detect_flag);
	}

	if (ct->op_game_up_count == 0) {
		ct->op_game_up_count = 1;
		ct->op_game_same_count = 0;
		ct->op_game_lost_count = 0;
		ct->op_game_detect_status = GAME_RTT_DETECT_INITIAL;
		ct->op_game_skb_len = skb->len;
		ct->op_game_timestamp = time_now;
	} else if (ct->op_game_detect_status == GAME_RTT_DETECT_INITIAL) {
		ct->op_game_timestamp = time_now;
		if (skb->len > 150)
			return;
		if (ct->op_game_skb_len == skb->len) {
			if (time_interval < 300 || time_interval > 10000)
				return;
			ct->op_game_same_count++;
		} else {
			ct->op_game_skb_len = skb->len;
			ct->op_game_same_count = 0;
			ct->op_game_down_count = 0;
		}

		if (op_sla_debug) {
			pr_info("[op_sla] %s: interval_time:%llu up_count:%d\n",
				__func__, time_interval, ct->op_game_up_count);
			pr_info("[op_sla] %s: down count:%d same count:%d\n",
				__func__,
				ct->op_game_down_count, ct->op_game_same_count);
			pr_info("[op_sla] %s: ct->op_game_skb_len:%d\n",
				__func__, ct->op_game_skb_len);
		}

		if (ct->op_game_down_count >= same_count_max &&
		    ct->op_game_same_count >= same_count_max) {
			reset_sla_game_app_rtt(game_type);
			ct->op_game_last_timestamp = time_now;
			ct->op_game_time_interval = time_interval;
			ct->op_game_detect_status = GAME_RTT_DETECTED_STREAM;
			ct->op_game_up_count++;
			return;
		}

		if (ct->op_game_up_count >= up_max_count) {
			ct->op_game_detect_status = GAME_SKB_COUNT_ENOUGH;
			if (op_sla_debug) {
				pr_info("[op_sla] %s: GAME_SKB_COUNT_ENOUGH!!\n",
					__func__);
			}
		}
		ct->op_game_up_count++;

	} else if (ct->op_game_detect_status == GAME_RTT_DETECTED_STREAM) {
		if (skb->len > 150)
			return;
		if (game_type == GAME_CJZC && skb->len == 123)
			return;
		set_game_rtt_stream_up_info(ct, time_now, game_type);
	}
}

static int mark_game_app_skb(struct nf_conn *ct, struct sk_buff *skb,
			     enum ip_conntrack_info ctinfo)
{
	int game_type = ct->op_app_type;
	struct iphdr *iph = NULL;
	u32 ct_mark = ct->mark & GAME_UNSPEC_MASK;
	int ret = SLA_SKB_ACCEPT;

	if (!game_start_state &&
	    (ct_mark & GAME_UNSPEC_MARK))
		return SLA_SKB_ACCEPT;

	iph = ip_hdr(skb);
	if (iph && (iph->protocol == IPPROTO_UDP ||
		    iph->protocol == IPPROTO_TCP)) {
		ct_mark	= ct->mark & MARK_MASK;

		if ((game_type == GAME_CJZC || game_type == GAME_WZRY) &&
		    iph->protocol == IPPROTO_TCP &&
		    ((XT_STATE_BIT(ctinfo) &
		      XT_STATE_BIT(IP_CT_ESTABLISHED)) ||
		    (XT_STATE_BIT(ctinfo) & XT_STATE_BIT(IP_CT_RELATED)))) {
			if (ct_mark == WLAN_MARK &&
			    op_sla_info[WLAN_INDEX].wlan_score > 40) {
				return SLA_SKB_ACCEPT;
			} else if (ct_mark == CELLULAR_MARK) {
				skb->mark = CELLULAR_MARK;
				return SLA_SKB_MARKED;
			}
		}

		skb->mark = op_sla_game_app_list.mark[game_type];

		if (ct_mark && skb->mark &&
		    ct_mark != skb->mark) {
			if (op_sla_debug) {
				pr_info("[op_sla] %s: reset ct proto:%u game type:%d\n",
					__func__, iph->protocol, game_type);
				pr_info("[op_sla] %s: ct mark:%x skb mark:%x\n",
					__func__, ct_mark, skb->mark);
			}
			if (!nf_ct_is_dying(ct) &&
			    nf_ct_is_confirmed(ct)) {
				nf_ct_kill(ct);
				return SLA_SKB_DROP;
			}
			skb->mark = ct_mark;
		}

		if (!ct_mark)
			ct->mark = (ct->mark & RTT_MASK) |
				   op_sla_game_app_list.mark[game_type];
		ret = SLA_SKB_MARKED;
	}

	return ret;
}

static bool is_game_app_skb(struct nf_conn *ct, struct sk_buff *skb,
			    enum ip_conntrack_info ctinfo)
{
	int game_type = 0;
	kuid_t uid;
	struct sock *sk = NULL;
	struct iphdr *iph = NULL;
	const struct file *filp = NULL;
	int app_type = get_app_type(ct);
	int total = op_sla_game_app_list.count + GAME_BASE;

	if (app_type == INIT_APP_TYPE) {
		sk = skb_to_full_sk(skb);
		if (!sk || !sk->sk_socket)
			return false;

		filp = sk->sk_socket->file;
		if (!filp)
			return false;

		iph = ip_hdr(skb);
		for (game_type = GAME_BASE; game_type < total; game_type++) {
			if (!op_sla_game_app_list.uid[game_type])
				return false;

			uid = make_kuid(&init_user_ns,
					op_sla_game_app_list.uid[game_type]);

			if (!uid_eq(filp->f_cred->fsuid, uid))
				return false;

			ct->op_app_type = game_type;
			if (op_sla_enable &&
			    op_sla_game_app_list.mark[game_type] ==
			    CELLULAR_MARK)
				op_sla_game_app_list.cell_bytes[game_type]
					+= skb->len;

			if (!game_start_state &&
			    iph && IPPROTO_TCP ==
			    iph->protocol) {
				ct->mark = (ct->mark &
					    RTT_MASK) |
					    WLAN_MARK;
				ct->mark |= GAME_UNSPEC_MARK;
			} else {
				ct->mark =
					(ct->mark & RTT_MASK) |
					 op_sla_game_app_list.mark[game_type];
			}
			return true;
		}
	} else if (app_type == GAME_TYPE) {
		game_type = ct->op_app_type;
		if (op_sla_enable &&
		    !op_sla_def_net &&
		    op_sla_game_app_list.mark[game_type] == CELLULAR_MARK)
			op_sla_game_app_list.cell_bytes[game_type] += skb->len;
		return true;
	}

	return false;
}

static void detect_game_up_skb(struct sk_buff *skb)
{
	struct timeval tv;
	struct iphdr *iph = NULL;
	struct nf_conn *ct = NULL;
	enum ip_conntrack_info ctinfo;

	if (!op_sla_rtt_detect)
		return;

	ct = nf_ct_get(skb, &ctinfo);
	if (!ct)
		return;

	if (!is_game_app_skb(ct, skb, ctinfo))
		return;

	do_gettimeofday(&tv);
	game_online_info.game_online = true;
	game_online_info.last_game_skb_tv = tv;

	//TCP and udp need to switch network
	iph = ip_hdr(skb);
	if (iph && iph->protocol == IPPROTO_UDP) {
		//only udp packet can active switch network to void updating
		//game with cell.
		sla_game_write_lock();
		game_app_switch_network(ct, skb);
		sla_game_write_unlock();

		detect_game_rtt_stream(ct, skb, ctinfo);
		detect_game_tx_stream(ct, skb, ctinfo);
	}
}

static void record_sla_app_cell_bytes(struct nf_conn *ct, struct sk_buff *skb)
{
	int index = 0;
	u32 ct_mark = 0x0;
	int app_type = get_app_type(ct);

	//calc game or white list app cell bytes
	if (op_sla_enable &&
	    !op_sla_def_net) {
		if (app_type == GAME_TYPE) {
			index = ct->op_app_type;
			if (op_sla_game_app_list.mark[index] ==
			    CELLULAR_MARK) {
				op_sla_game_app_list.cell_bytes[index] +=
					skb->len;
			}
		} else if (app_type == WHITE_APP_TYPE) {
			ct_mark = ct->mark & MARK_MASK;
			if (ct_mark == CELLULAR_MARK) {
				index = ct->op_app_type - WHITE_APP_BASE;
				if (index < WHITE_APP_NUM) {
					white_app_list.cell_bytes[index] +=
						skb->len;
				}
			}
		}
		//reset dev retran num
		reset_dev_info_syn_retran(ct, skb);
	}

	//calc white app cell bytes when sla is not enable
	if ((op_sla_info[CELLULAR_INDEX].if_up ||
	     op_sla_info[CELLULAR_INDEX].need_up) &&
	    (!op_sla_info[WLAN_INDEX].if_up || op_sla_def_net) &&
	    app_type == WHITE_APP_TYPE) {
		index = ct->op_app_type - WHITE_APP_BASE;
		if (index < WHITE_APP_NUM)
			white_app_list.cell_bytes_normal[index] += skb->len;
	}
}

static void rtt_game_check(struct nf_conn *ct, struct sk_buff *skb)
{
	s64 time_now = ktime_get_ns() / 1000000;
	int game_rtt = 0;
	struct iphdr *iph = ip_hdr(skb);
	int app_type = get_app_type(ct);
	int game_type = ct->op_app_type;
	bool cell_quality_good = get_ct_cell_quality(skb, game_type);

	if (op_sla_debug) {
		if (iph && iph->protocol == IPPROTO_UDP &&
		    app_type == GAME_TYPE) {
			pr_info("[op_sla] %s: skb dev:%s game_status:%u\n",
				__func__, skb->dev->name,
				ct->op_game_detect_status);
			pr_info("[op_sla] %s: game type:%d lost count:%d interval_time:%u\n",
				__func__, game_type, ct->op_game_lost_count,
				ct->op_game_time_interval);
			pr_info("[op_sla] %s: game mark:%x protp:%d src_port:%d skb len:%d\n",
				__func__, op_sla_game_app_list.mark[game_type],
				iph->protocol, ntohs(udp_hdr(skb)->dest),
				skb->len);
			pr_info("[op_sla] %s: timeStamp = %llu\n",
				__func__, ct->op_game_timestamp);
		}
	}

	if (game_type == GAME_PUBG || game_type == GAME_PUBG_TW ||
	    game_type == GAME_AOV || game_type == GAME_QQ_CAR_TW) {
		if (iph && iph->protocol == IPPROTO_UDP)
			game_rtt_wan_detect_flag = 0;
	}

	if (app_type == GAME_TYPE)
		ct->op_game_down_count++;

	if (ct->op_game_detect_status == GAME_RTT_DETECTED_STREAM &&
	    iph && iph->protocol == IPPROTO_UDP && ct->op_game_timestamp) {
		if (time_now > ct->op_game_timestamp) {
			if (skb->len > 150)
				return;
			game_rtt = (int)(time_now - ct->op_game_timestamp);
			if (game_rtt < 0) {
				if (op_sla_debug) {
					pr_info("[op_sla] %s: invalid RTT:%dms\n",
						__func__,  game_rtt);
				}
				ct->op_game_timestamp = 0;
				return;
			}
		}
		ct->op_game_timestamp = 0;
		if (game_type == GAME_WZRY) {
			if (ct->op_game_time_interval < 1000 &&
			    op_sla_game_app_list.mark[game_type] ==
			    CELLULAR_MARK) {
				if (op_sla_debug) {
					pr_info("[op_sla] %s: game rtt interval error for cell:%d\n",
						__func__, game_rtt);
				}
				return;
			}
			if (game_rtt > MAX_GAME_RTT) {
				if (op_sla_debug) {
					pr_info("[op_sla] %s: game rtt max, game_rtt:%d\n",
						__func__, game_rtt);
				}
				game_rtt = MAX_GAME_RTT;
			}
		} else {
			if (ct->op_game_time_interval < 200)
				return;
		}
		if (!enable_to_user && !op_sla_enable &&
		    sla_switch_enable && cell_quality_good) {
			if (op_sla_debug) {
				pr_info("[op_sla] %s: send SLA_ENABLE\n",
					__func__);
			}
			prepare_enable_sla();
		}
		if (op_sla_debug) {
			pr_info("[op_sla] %s: game_rtt = %d\n", __func__,
				op_sla_game_app_list.rtt[game_type]);
		}
		ct->op_game_lost_count = 0;
		sla_game_write_lock();
		game_rtt_estimator(game_type, game_rtt, ct);
		sla_game_write_unlock();
	}
}

static void rx_interval_error_check(struct nf_conn *ct, struct sk_buff *skb)
{
	s64 time_now = ktime_get_ns() / 1000000;
	int rx_interval = 0;
	int special_rx_interval_error = 0;
	struct iphdr *iph = ip_hdr(skb);
	int game_type = ct->op_app_type;
	int app_type = get_app_type(ct);
	bool cell_quality_good = get_ct_cell_quality(skb, game_type);

	if (app_type == GAME_TYPE)
		ct->op_game_rx_normal_time_record = time_now;

	if (game_type == GAME_QQ_CAR && skb->len == 83 &&
	    (iph && iph->protocol == IPPROTO_UDP)) {
		if (game_type == GAME_QQ_CAR)
			ct->op_game_special_rx_pkt_interval = 10000;
		ct->op_game_special_rx_pkt_timestamp = time_now;
		if (ct->op_game_special_rx_pkt_last_timestamp) {
			rx_interval =
				(int)(ct->op_game_special_rx_pkt_timestamp
				- ct->op_game_special_rx_pkt_last_timestamp);
			special_rx_interval_error =
				abs(rx_interval -
				ct->op_game_special_rx_pkt_interval);
			ct->op_game_special_rx_pkt_last_timestamp =
				ct->op_game_special_rx_pkt_timestamp;
			sla_game_rx_error_write_lock();
			rx_interval_error_estimator(game_type,
						    special_rx_interval_error,
						    ct);
			sla_game_rx_error_write_unlock();
		} else {
			reset_sla_game_app_rx_error(game_type);
			ct->op_game_special_rx_pkt_last_timestamp =
				ct->op_game_special_rx_pkt_timestamp;
		}
		if (op_sla_debug) {
			pr_info("[op_sla] %s: rx_interval:%dms\n",
				__func__, rx_interval);
			pr_info("[op_sla] %s: special_rx_interval_error:%dms\n",
				__func__, special_rx_interval_error);
		}
		if (!enable_to_user && !op_sla_enable &&
		    sla_switch_enable && cell_quality_good) {
			if (op_sla_debug) {
				pr_info("[op_sla] %s: send SLA_ENABLE\n",
					__func__);
			}
			prepare_enable_sla();
		}
	}

	// This method is not stable for diff area.
	// Add log for debugging this problem.
	if (game_type == GAME_WZRY && skb->len == 100 &&
	    (iph && iph->protocol == IPPROTO_UDP)) {
		if (game_type == GAME_WZRY)
			ct->op_game_special_rx_pkt_interval = 2000;
		ct->op_game_special_rx_pkt_timestamp = time_now;
		if (ct->op_game_special_rx_pkt_last_timestamp) {
			rx_interval =
				(int)(ct->op_game_special_rx_pkt_timestamp
				- ct->op_game_special_rx_pkt_last_timestamp);
			special_rx_interval_error =
				abs(rx_interval -
				ct->op_game_special_rx_pkt_interval);
			ct->op_game_special_rx_pkt_last_timestamp =
				ct->op_game_special_rx_pkt_timestamp;
		} else {
			reset_sla_game_app_rx_error(game_type);
			ct->op_game_special_rx_pkt_last_timestamp =
				ct->op_game_special_rx_pkt_timestamp;
		}
		if (op_sla_debug) {
			pr_info("[op_sla] %s: rx_interval:%dms\n",
				__func__, rx_interval);
			pr_info("[op_sla] %s: special_rx_interval_error:%dms\n",
				__func__, special_rx_interval_error);
		}
		if (!enable_to_user && !op_sla_enable &&
		    sla_switch_enable && cell_quality_good) {
			if (op_sla_debug) {
				pr_info("[op_sla] %s: send SLA_ENABLE\n",
					__func__);
			}
			prepare_enable_sla();
		}
	}
}

static unsigned int op_sla_rx_calc(void *priv,
				   struct sk_buff *skb,
				   const struct nf_hook_state *state)
{
	struct nf_conn *ct = NULL;
	enum ip_conntrack_info ctinfo;

	if (!op_sla_rtt_detect)
		return NF_ACCEPT;
	ct = nf_ct_get(skb, &ctinfo);
	if (!ct)
		return NF_ACCEPT;
	//calc game or white list app cell bytes
	record_sla_app_cell_bytes(ct, skb);
	rtt_game_check(ct, skb);
	rx_interval_error_check(ct, skb);
	return NF_ACCEPT;
}

static bool is_skb_pre_bound(struct sk_buff *skb)
{
	u32 pre_mark = skb->mark & 0x10000;

	if (pre_mark == 0x10000)
		return true;
	return false;
}

static int sla_skb_reroute(struct sk_buff *skb, struct nf_conn *ct,
			   const struct nf_hook_state *state)
{
	int err;

	err = ip_route_me_harder(state->net, skb, RTN_UNSPEC);
	if (err < 0)
		return NF_DROP_ERR(err);
	if (op_sla_debug)
		pr_info("[op_sla] %s: skb->mark=%x\n", __func__, skb->mark);
	return NF_ACCEPT;
}

static int dns_skb_need_sla(struct nf_conn *ct, struct sk_buff *skb)
{
	int ret = SLA_SKB_CONTINUE;
	struct iphdr *iph = NULL;
	bool cell_quality_good = get_ct_cell_quality(skb, ct->op_app_type);

	iph = ip_hdr(skb);
	if (iph &&
	    (iph->protocol == IPPROTO_TCP || iph->protocol == IPPROTO_UDP) &&
	    ntohs(udp_hdr(skb)->dest) == 53) {
		ret = SLA_SKB_ACCEPT;
		if (cell_quality_good &&
		    (op_sla_info[WLAN_INDEX].weight_state ==
		    WEIGHT_STATE_USELESS ||
		    (op_sla_info[CELLULAR_INDEX].max_speed >= 100 &&
		    op_sla_info[WLAN_INDEX].wlan_score_bad_count >=
		    WLAN_SCORE_BAD_NUM))) {
			skb->mark = CELLULAR_MARK;
			ret = SLA_SKB_MARKED;
		}
	}
	return ret;
}

static void detect_white_list_app_skb(struct sk_buff *skb)
{
	int i = 0;
	int index = -1;
	kuid_t uid;
	struct nf_conn *ct = NULL;
	enum ip_conntrack_info ctinfo;
	struct sock *sk = NULL;
	const struct file *filp = NULL;
	int app_type;

	ct = nf_ct_get(skb, &ctinfo);

	if (!ct)
		return;

	app_type = get_app_type(ct);

	if (app_type == INIT_APP_TYPE) {
		sk = skb_to_full_sk(skb);
		if (!sk || !sk->sk_socket)
			return;

		filp = sk->sk_socket->file;

		if (!filp)
			return;

		for (i = 0; i < white_app_list.count; i++) {
			if (white_app_list.uid[i]) {
				uid = make_kuid(&init_user_ns,
						white_app_list.uid[i]);
				if (uid_eq(filp->f_cred->fsuid, uid)) {
					ct->op_app_type = i + WHITE_APP_BASE;
					return;
				}
			}
		}
	} else if (app_type == WHITE_APP_TYPE &&
		  (op_sla_info[CELLULAR_INDEX].if_up ||
		   op_sla_info[CELLULAR_INDEX].need_up)) {
		//calc white app cell bytes when sla is not enable
		if (!op_sla_info[WLAN_INDEX].if_up || op_sla_def_net) {
			index = ct->op_app_type - WHITE_APP_BASE;
			if (index < WHITE_APP_NUM) {
				white_app_list.cell_bytes_normal[index] +=
					skb->len;
			}
		}
	}
}

static int handle_white_app_app_skb(struct nf_conn *ct, struct sk_buff *skb,
				    enum ip_conntrack_info ctinfo,
				    const struct nf_hook_state *state)
{
	int index = 0;
	struct sock *sk = NULL;
	int retran_mark = 0;
	u32 ct_mark = 0x0;
	int ret = SLA_SKB_CONTINUE;

	if (ctinfo == IP_CT_NEW) {
		if (op_sla_debug)
			pr_info("[op_sla] %s: IP_CT_NEW\n", __func__);
		sk = skb_to_full_sk(skb);
		if (sk) {
			ret = syn_retransmits_packet_do_specail(sk, ct, skb);
			if (ret == SLA_SKB_MARKED)
				return sla_skb_reroute(skb, ct, state);
			else if (ret == SLA_SKB_REMARK)
				return NF_DROP;
			else if (ret == SLA_SKB_ACCEPT)
				return NF_ACCEPT;

			retran_mark = mark_retransmits_syn_skb(sk, ct);
			if (retran_mark) {
				if (op_sla_debug)
					pr_info("[op_sla] %s: retran mark:%x\n",
						__func__,
						retran_mark & MARK_MASK);
				skb->mark = retran_mark;
			} else {
				sla_read_lock();
				if (op_sla_info[WLAN_INDEX].weight == 0 &&
				    op_sla_info[CELLULAR_INDEX].weight == 0) {
					skb->mark = set_skb_mark_by_default();
				} else {
					skb->mark = get_skb_mark_by_weight();
				}
				sla_read_unlock();
			}

			ct->mark = skb->mark;
			sk->op_sla_mark = skb->mark;
		}
	} else if ((XT_STATE_BIT(ctinfo) & XT_STATE_BIT(IP_CT_ESTABLISHED)) ||
		   (XT_STATE_BIT(ctinfo) & XT_STATE_BIT(IP_CT_RELATED))) {
		skb->mark = ct->mark & MARK_MASK;
	}

	//calc white list app cell bytes
	ct_mark = ct->mark & MARK_MASK;
	if (ct_mark == CELLULAR_MARK) {
		index = ct->op_app_type - WHITE_APP_BASE;
		if (index < WHITE_APP_NUM)
			white_app_list.cell_bytes[index] += skb->len;
	}
	return sla_skb_reroute(skb, ct, state);
}

static int handle_game_app_skb(struct nf_conn *ct, struct sk_buff *skb,
			       enum ip_conntrack_info ctinfo,
			       const struct nf_hook_state *state)
{
	int ret = SLA_SKB_CONTINUE;

	ret = mark_game_app_skb(ct, skb, ctinfo);
	if (ret == SLA_SKB_MARKED)
		return sla_skb_reroute(skb, ct, state);
	else if (ret == SLA_SKB_ACCEPT)
		return NF_ACCEPT;
	else if (ret == SLA_SKB_DROP)
		return NF_DROP;
	return NF_ACCEPT;
}

static int sla_mark_skb(struct sk_buff *skb, const struct nf_hook_state *state)
{
	struct nf_conn *ct = NULL;
	int ret = SLA_SKB_CONTINUE;
	enum ip_conntrack_info ctinfo;
	int app_type;

	//if wlan assistant has change network to cell, do not mark SKB
	if (op_sla_def_net)
		return NF_ACCEPT;

	ct = nf_ct_get(skb, &ctinfo);

	if (!ct)
		return NF_ACCEPT;
	// when the wifi is poor,the dns request allways can not rcv respones,
	// so please let the dns packet with the cell network mark.
	ret = dns_skb_need_sla(ct, skb);
	if (ret == SLA_SKB_MARKED)
		return sla_skb_reroute(skb, ct, state);
	else if (ret == SLA_SKB_ACCEPT)
		return NF_ACCEPT;

	if (is_skb_pre_bound(skb))
		return NF_ACCEPT;

	app_type = get_app_type(ct);

	if (app_type == GAME_TYPE)
		return handle_game_app_skb(ct, skb, ctinfo, state);
	else if (app_type == WHITE_APP_TYPE)
		return handle_white_app_app_skb(ct, skb, ctinfo, state);

	return NF_ACCEPT;
}

// op sla hook function, mark skb and rerout skb
static unsigned int op_sla(void *priv,
			   struct sk_buff *skb,
			   const struct nf_hook_state *state)
{
	int ret = NF_ACCEPT;

	struct nf_conn *ct = NULL;
	enum ip_conntrack_info ctinfo;

	ct = nf_ct_get(skb, &ctinfo);
	if (!ct)
		return NF_ACCEPT;

	detect_game_up_skb(skb);

	//we need to calc white list app cell bytes when sla not enabled
	detect_white_list_app_skb(skb);

	if (op_sla_enable) {
		ret = sla_mark_skb(skb, state);
	} else {
		if (!sla_screen_on)
			return ret;
		if (op_sla_info[WLAN_INDEX].if_up) {
			ret = get_wlan_syn_retran(skb);
			if (op_sla_calc_speed &&
			    !op_speed_info[WLAN_INDEX].speed_done) {
				ret = wlan_get_speed_prepare(skb);
			}
		}
	}
	return ret;
}

// how can we calc speed when app connect server with 443 dport(https)
static void get_content_length(struct nf_conn *ct, struct sk_buff *skb,
			       int header_len, int index)
{
	char *p = (char *)skb->data + header_len;
	char *start = NULL;
	char *end = NULL;
	int temp_len = 0;
	u64 tmp_time;
	u32 content_len = 0;
	char data_buf[256];
	char data_len[11];
	int ret;

	memset(data_len, 0x0, sizeof(data_len));
	memset(data_buf, 0x0, sizeof(data_buf));

	if (ct->op_http_flag != 1 || ct->op_skb_count > 3)
		return;

	ct->op_skb_count++;

	temp_len = (char *)skb_tail_pointer(skb) - p;
	if (temp_len < 25) {//HTTP/1.1 200 OK + Content-Length
		return;
	}

	p += 25;

	temp_len = (char *)skb_tail_pointer(skb) - p;
	if (temp_len) {
		if (temp_len > (sizeof(data_buf) - 1))
			temp_len = (sizeof(data_buf) - 1);
		memcpy(data_buf, p, temp_len);
		start = strnstr(data_buf, "Content-Length", strlen(data_buf));
		if (start) {
			ct->op_http_flag = 2;
			start += 16; //add Content-Length:

			end = strnchr(start, 0x0d, strlen(start));//get '\r\n'

			if (end) {
				if ((end - start) < 11) {
					memcpy(data_len, start, end - start);
					ret = kstrtou32(data_len, 0, &index);
					//pr_info("[op_sla] get_content_length:"
					//" content = %u\n", content_len);
				} else {
					content_len = 0x7FFFFFFF;
				}

				tmp_time = ktime_get_ns();
				op_speed_info[index].sum_bytes += content_len;
				if (op_speed_info[index].bytes_time == 0) {
					op_speed_info[index].bytes_time =
						tmp_time;
				} else if (op_speed_info[index].sum_bytes >=
					   20000 || (tmp_time -
					   op_speed_info[index].bytes_time) >
					   5000000000) {
					op_speed_info[index].bytes_time =
						tmp_time;
					op_speed_info[index].sum_bytes = 0;
					if (op_speed_info[index].sum_bytes <
					    20000)
						return;
					op_speed_info[index].ms_speed_flag = 1;
				}
			}
		}
	}
}

static unsigned int op_sla_speed_calc(void *priv,
				      struct sk_buff *skb,
				      const struct nf_hook_state *state)
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct = NULL;
	int index = -1;
	int tmp_speed = 0;
	u64 time_now = 0;
	u64 tmp_time = 0;
	struct iphdr *iph;
	struct tcphdr *tcph;
	int datalen = 0;
	int header_len = 0;
	int game_type;
	bool cell_quality_good;

	if (!sla_switch_enable)
		return NF_ACCEPT;

	//only calc wlan speed
	if (op_sla_info[WLAN_INDEX].if_up &&
	    !op_sla_info[CELLULAR_INDEX].if_up) {
		index = WLAN_INDEX;
	} else {
		return NF_ACCEPT;
	}

	ct = nf_ct_get(skb, &ctinfo);

	if (!ct)
		return NF_ACCEPT;

	iph = ip_hdr(skb);
	if (!enable_to_user &&
	    op_sla_calc_speed &&
	    !op_speed_info[index].speed_done &&
	    (XT_STATE_BIT(ctinfo) & XT_STATE_BIT(IP_CT_ESTABLISHED)) &&
	    (iph && iph->protocol == IPPROTO_TCP)) {
		game_type = ct->op_app_type;
		cell_quality_good = get_ct_cell_quality(skb, game_type);
		tcph = tcp_hdr(skb);
		datalen = ntohs(iph->tot_len);
		header_len = iph->ihl * 4 + tcph->doff * 4;

		if ((datalen - header_len) >= 64) {//ip->len > tcphdrlen
			if (!op_speed_info[index].ms_speed_flag)
				get_content_length(ct, skb, header_len, index);

			if (op_speed_info[index].ms_speed_flag) {
				time_now = ktime_get_ns();
				if (op_speed_info[index].last_time == 0 &&
				    op_speed_info[index].first_time == 0) {
					op_speed_info[index].last_time =
						time_now;
					op_speed_info[index].first_time =
						time_now;
					op_speed_info[index].rx_bytes =
						skb->len;
					return NF_ACCEPT;
				}

				tmp_time = time_now -
					op_speed_info[index].first_time;
				op_speed_info[index].rx_bytes += skb->len;
				op_speed_info[index].last_time = time_now;

				if (tmp_time <= 500000000)
					return NF_ACCEPT;

				op_speed_info[index].ms_speed_flag = 0;
				tmp_time =
					op_speed_info[index].last_time -
					op_speed_info[index].first_time;
				tmp_speed =
					(1000 * 1000 *
					op_speed_info[index].rx_bytes) /
					(tmp_time); //kB/s

				op_speed_info[index].speed_done = 1;
				do_gettimeofday(&last_calc_small_speed_tv);
				if (cell_quality_good &&
				    tmp_speed >
				    CALC_WEIGHT_MIN_SPEED_1 &&
				    tmp_speed <
				    CALC_WEIGHT_MIN_SPEED_3 &&
				    op_sla_info[index].max_speed <
				    100) {
					pr_info("[op_sla] %s: send SLA_ENABLE\n",
						__func__);
					prepare_enable_sla();
				}
				pr_info("[op_sla] %s: speed[%d] = %d\n",
					__func__, index, tmp_speed);
			}
		}
	}
	return NF_ACCEPT;
}

static void op_statistic_dev_rtt(struct sock *sk, long rtt)
{
	int index = -1;
	int tmp_rtt = rtt / 1000; //us -> ms
	u32 mark = sk->op_sla_mark & MARK_MASK;

	if (op_sla_def_net) {
		index  = CELLULAR_INDEX;
	} else if (!op_sla_enable) {
		if (op_sla_info[WLAN_INDEX].if_up) {
			index = WLAN_INDEX;
		} else if (op_sla_info[CELLULAR_INDEX].if_up ||
			   op_sla_info[CELLULAR_INDEX].need_up) {
			index = CELLULAR_INDEX;
		}
	} else {
		index = find_iface_index_by_mark(mark);
	}

	if (index != -1)
		calc_rtt_by_dev_index(index, tmp_rtt);
}

static void is_need_calc_wlan_small_speed(int speed)
{
	if (sla_screen_on &&
	    !enable_to_user &&
	    !op_sla_enable &&
	    !op_sla_calc_speed) {
		if (speed <= 100 &&
		    speed > CALC_WEIGHT_MIN_SPEED_1 &&
		    op_sla_info[WLAN_INDEX].minute_speed > MINUTE_LITTE_RATE) {
			op_sla_calc_speed = 1;
			memset(&op_speed_info[WLAN_INDEX], 0x0,
			       sizeof(struct op_speed_calc));
		}
	}
}

static void auto_disable_sla_for_good_wlan(void)
{
	int time_interval;
	struct timeval tv;

	do_gettimeofday(&tv);
	time_interval = tv.tv_sec - last_enable_cellular_tv.tv_sec;

	if (enable_to_user &&
	    op_sla_enable &&
	    time_interval >= 300 &&
	    !game_online_info.game_online &&
	    op_sla_info[WLAN_INDEX].sla_avg_rtt < 150 &&
	    op_sla_info[WLAN_INDEX].max_speed >= 300) {
		enable_to_user = 0;
		disable_cellular_tv = tv;

		sla_write_lock();
		op_sla_info[WLAN_INDEX].weight = 100;
		op_sla_info[CELLULAR_INDEX].weight = 0;
		op_sla_info[CELLULAR_INDEX].need_disable = true;
		sla_write_unlock();

		if (op_sla_debug)
			pr_info("[op_sla] %s: need to disable cell\n",
				__func__);
	}
}

static void detect_wlan_state_with_speed(void)
{
	auto_disable_sla_for_good_wlan();
	is_need_calc_wlan_small_speed(op_sla_info[WLAN_INDEX].max_speed);
}

static inline int dev_isalive(const struct net_device *dev)
{
	return dev->reg_state <= NETREG_REGISTERED;
}

static void statistic_dev_speed(struct timeval tv, int time_interval)
{
	int i = 0;
	int temp_speed;
	u64 temp_bytes;
	int tmp_minute_time;
	int tmp_minute_speed;
	int do_calc_minute_speed = 0;
	struct net_device *dev;
	const struct rtnl_link_stats64 *stats;
	struct rtnl_link_stats64 temp;

	tmp_minute_time = tv.tv_sec - last_minute_speed_tv.tv_sec;
	if (tmp_minute_time >= LITTLE_FLOW_TIME) {
		last_minute_speed_tv = tv;
		do_calc_minute_speed = 1;
	}

	for (i = 0; i < IFACE_NUM; i++) {
		dev = dev_get_by_name(&init_net, op_sla_info[i].dev_name);
		if (dev)
			stats = dev_get_stats(dev, &temp);
		if ((op_sla_info[i].if_up || op_sla_info[i].need_up) && dev &&
		    (dev_isalive(dev) && stats)) {
			// first time have no value, and maybe
			// op_sla_info[i].rx_bytes will more
			// than stats->rx_bytes
			if (op_sla_info[i].rx_bytes == 0 ||
			    op_sla_info[i].rx_bytes > stats->rx_bytes) {
				op_sla_info[i].rx_bytes = stats->rx_bytes;
				op_sla_info[i].minute_rx_bytes =
					stats->rx_bytes;
			} else {
				if (do_calc_minute_speed) {
					if (i == WLAN_INDEX)
						detect_wlan_state_with_speed();
					temp_bytes = stats->rx_bytes -
						op_sla_info[i].minute_rx_bytes;
					op_sla_info[i].minute_rx_bytes =
						stats->rx_bytes;
					tmp_minute_speed = (8 * temp_bytes)
						/ tmp_minute_time; //kbit/s
					op_sla_info[i].minute_speed =
						tmp_minute_speed / 1000;
				}

				temp_bytes = stats->rx_bytes -
					op_sla_info[i].rx_bytes;
				op_sla_info[i].rx_bytes = stats->rx_bytes;

				temp_speed = (temp_bytes / time_interval)
					/ 1000; //kB/s
				op_sla_info[i].current_speed = temp_speed;

				if (temp_speed > DOWNLOAD_SPEED &&
				    temp_speed >
				    op_sla_info[i].max_speed / 2 &&
				    op_sla_info[i].download_flag <
				    DOWN_LOAD_FLAG) {
					op_sla_info[i].download_flag++;
				} else if (temp_speed <
					   (op_sla_info[i].max_speed / 3)) {
					op_sla_info[i].download_flag = 0;
				}

				if (temp_speed > op_sla_info[i].max_speed)
					op_sla_info[i].max_speed = temp_speed;

				if (i == CELLULAR_INDEX &&
				    op_sla_info[i].max_speed >
				    sla_params_info.cell_speed) {
					op_sla_info[i].max_speed =
						sla_params_info.cell_speed;
				}

				if (i == WLAN_INDEX &&
				    op_sla_info[i].wlan_score <=
				    sla_params_info.wlan_bad_score &&
				    op_sla_info[i].max_speed >
				    sla_params_info.wlan_little_score_speed) {
					op_sla_info[i].max_speed =
					sla_params_info.wlan_little_score_speed;
				} else if (i == WLAN_INDEX &&
					   op_sla_info[i].max_speed >
					   sla_params_info.wlan_speed) {
					op_sla_info[i].max_speed =
						sla_params_info.wlan_speed;
				}

				// for lte current_speed may bigger
				// than max_speed
				if (op_sla_info[i].current_speed >
				    op_sla_info[i].max_speed) {
					op_sla_info[i].left_speed =
						op_sla_info[i].current_speed -
						op_sla_info[i].max_speed;
				} else {
					op_sla_info[i].left_speed =
						op_sla_info[i].max_speed -
						op_sla_info[i].current_speed;
				}

				if (temp_speed > 400)
					op_sla_info[i].congestion_flag =
						CONGESTION_LEVEL_NORMAL;

				if (temp_speed > 400 &&
				    op_sla_info[i].avg_rtt > 150) {
					op_sla_info[i].avg_rtt -= 50;
					op_sla_info[i].sla_avg_rtt -= 50;
				}
			}

			if (op_sla_debug) {
				pr_info("[op_sla] %s: dev_name:%s if_up:%d\n",
					__func__, op_sla_info[i].dev_name,
					op_sla_info[i].if_up);
				pr_info("[op_sla] %s: max_speed:%d current_speed:%d\n",
					__func__, op_sla_info[i].max_speed,
					op_sla_info[i].current_speed);
				pr_info("[op_sla] %s: avg_rtt:%d congestion:%d\n",
					__func__, op_sla_info[i].avg_rtt,
					op_sla_info[i].congestion_flag);
				pr_info("[op_sla] %s: is_download:%d syn_retran:%d\n",
					__func__, op_sla_info[i].download_flag,
					op_sla_info[i].syn_retran);
				pr_info("[op_sla] %s: minute_speed:%d weight_state:%d\n",
					__func__, op_sla_info[i].minute_speed,
					op_sla_info[i].weight_state);
			}
		}
		if (dev)
			dev_put(dev);
	}
}

static void reset_dev_info_by_retran(struct op_dev_info *node)
{
	struct timeval tv;

	//to avoid when weight_state change WEIGHT_STATE_USELESS now ,
	//but the next moment change to WEIGHT_STATE_RECOVERY
	//because of minute_speed is little than MINUTE_LITTE_RATE;
	do_gettimeofday(&tv);
	last_minute_speed_tv = tv;
	node->minute_speed = MINUTE_LITTE_RATE;

	sla_rtt_write_lock();
	node->rtt_index = 0;
	node->sum_rtt = 0;
	node->avg_rtt = 0;
	node->sla_avg_rtt = 0;
	node->sla_rtt_num = 0;
	node->sla_sum_rtt = 0;
	sla_rtt_write_unlock();

	node->max_speed = 0;
	node->left_speed = 0;
	node->current_speed = 0;
	node->syn_retran = 0;
	node->weight_state = WEIGHT_STATE_USELESS;

	sla_write_lock();
	node->weight = 0;
	sla_write_unlock();

	if (op_sla_debug) {
		pr_info("[op_sla] %s: dev_name = %s\n", __func__,
			node->dev_name);
	}
}

static void detect_network_is_available(void)
{
	int i;

	if (!op_sla_enable)
		return;

	for (i = 0; i < IFACE_NUM; i++) {
		if (op_sla_info[i].syn_retran >= MAX_SYN_RETRANS) {
			reset_dev_info_by_retran(&op_sla_info[i]);
			op_sla_info[i].congestion_flag = CONGESTION_LEVEL_HIGH;
		}
	}
}

static void change_weight_state(void)
{
	int i;

	if (!op_sla_enable)
		return;

	for (i = 0; i < IFACE_NUM; i++) {
		if (op_sla_debug) {
			pr_info("[op_sla] %s: index:%d weight_state:%d\n",
				__func__, i, op_sla_info[i].weight_state);
			pr_info("[op_sla] %s: max_speed:%d syn_retran:%d sla_avg_rtt:%d\n",
				__func__, op_sla_info[i].max_speed,
				op_sla_info[i].syn_retran,
				op_sla_info[i].sla_avg_rtt);
		}
		if (op_sla_info[i].weight_state != WEIGHT_STATE_NORMAL) {
			if (op_sla_info[i].max_speed >=
			    CALC_WEIGHT_MIN_SPEED_2) {
				op_sla_info[i].weight_state =
					WEIGHT_STATE_NORMAL;
			} else if ((op_sla_info[i].syn_retran ||
				    op_sla_info[i].sla_avg_rtt > NORMAL_RTT) &&
				    op_sla_info[i].weight_state ==
				    WEIGHT_STATE_RECOVERY) {
				reset_dev_info_by_retran(&op_sla_info[i]);
			}
		}
	}
}

static int calc_weight_with_speed(int wlan_speed, int cellular_speed)
{
	int tmp_weight;
	int sum_speed = wlan_speed + cellular_speed;

	tmp_weight = (100 * wlan_speed) / sum_speed;

	return tmp_weight;
}

static int calc_weight_with_left_speed(void)
{
	int wlan_speed;
	int cellular_speed;

	if (op_sla_info[WLAN_INDEX].download_flag == DOWN_LOAD_FLAG &&
	    (op_sla_info[WLAN_INDEX].congestion_flag >
	     CONGESTION_LEVEL_NORMAL ||
	     op_sla_info[CELLULAR_INDEX].congestion_flag >
	     CONGESTION_LEVEL_NORMAL) &&
	    (op_sla_info[WLAN_INDEX].max_speed > CALC_WEIGHT_MIN_SPEED_2 &&
	     op_sla_info[CELLULAR_INDEX].max_speed >
	     CALC_WEIGHT_MIN_SPEED_2)) {
		wlan_speed = op_sla_info[WLAN_INDEX].left_speed;
		cellular_speed = op_sla_info[CELLULAR_INDEX].left_speed;
		op_sla_info[WLAN_INDEX].weight =
			calc_weight_with_speed(wlan_speed, cellular_speed);
		op_sla_info[CELLULAR_INDEX].weight = 100;
		return 1;
	}
	return 0;
}

static bool is_wlan_speed_good(void)
{
	if (op_sla_info[WLAN_INDEX].max_speed >= 300 &&
	    op_sla_info[WLAN_INDEX].sla_avg_rtt < 150 &&
	    op_sla_info[WLAN_INDEX].wlan_score >= 60) {
		op_sla_info[WLAN_INDEX].weight = 100;
		op_sla_info[CELLULAR_INDEX].weight = 0;
		return true;
	}
	return false;
}

static void recalc_dev_weight(void)
{
	int wlan_speed = op_sla_info[WLAN_INDEX].max_speed;
	int cellular_speed = op_sla_info[CELLULAR_INDEX].max_speed;

	if (!op_sla_enable)
		return;

	sla_write_lock();

	if (op_sla_info[WLAN_INDEX].if_up &&
	    op_sla_info[CELLULAR_INDEX].if_up) {
		if (is_wlan_speed_good() ||
		    calc_weight_with_left_speed()) {
			goto calc_weight_finish;
		}

		if ((wlan_speed >= CALC_WEIGHT_MIN_SPEED_2 &&
		     cellular_speed >= CALC_WEIGHT_MIN_SPEED_2) ||
		    (wlan_speed >= CALC_WEIGHT_MIN_SPEED_1 &&
		     cellular_speed >= (3 * wlan_speed))) {
			op_sla_info[WLAN_INDEX].weight =
				calc_weight_with_speed(wlan_speed,
						       cellular_speed);
		} else if ((op_sla_info[WLAN_INDEX].congestion_flag ==
			   CONGESTION_LEVEL_MIDDLE ||
			   op_sla_info[CELLULAR_INDEX].congestion_flag ==
			   CONGESTION_LEVEL_MIDDLE) &&
			   (wlan_speed >= CALC_WEIGHT_MIN_SPEED_1 &&
			   cellular_speed >= CALC_WEIGHT_MIN_SPEED_1)) {
			op_sla_info[WLAN_INDEX].weight =
				calc_weight_with_speed(wlan_speed,
						       cellular_speed);
		} else if (op_sla_info[WLAN_INDEX].congestion_flag ==
			   CONGESTION_LEVEL_HIGH ||
			   op_sla_info[CELLULAR_INDEX].congestion_flag ==
			   CONGESTION_LEVEL_HIGH) {
			op_sla_info[WLAN_INDEX].weight =
				calc_weight_with_speed(wlan_speed,
						       cellular_speed);
		} else {
			op_sla_info[WLAN_INDEX].weight = 30;
			op_sla_info[CELLULAR_INDEX].weight = 100;
			goto calc_weight_finish;
		}
	} else if (op_sla_info[WLAN_INDEX].if_up) {
		op_sla_info[WLAN_INDEX].weight = 100;
	}

	if (op_sla_info[WLAN_INDEX].max_speed < CALC_WEIGHT_MIN_SPEED_1 &&
	    op_sla_info[WLAN_INDEX].congestion_flag == CONGESTION_LEVEL_HIGH) {
		op_sla_info[WLAN_INDEX].weight = 0;
	}
	op_sla_info[CELLULAR_INDEX].weight = 100;

calc_weight_finish:

	if (op_sla_info[WLAN_INDEX].max_speed <= 50 &&
	    op_sla_info[CELLULAR_INDEX].max_speed >= 200 &&
	    op_sla_info[WLAN_INDEX].wlan_score_bad_count >=
	    WLAN_SCORE_BAD_NUM) {
		op_sla_info[WLAN_INDEX].weight = 0;
		op_sla_info[CELLULAR_INDEX].weight = 100;
	}
	sla_write_unlock();
	if (op_sla_debug) {
		pr_info("[op_sla] %s: wifi weight = %d, cellular weight = %d\n",
			__func__, op_sla_info[WLAN_INDEX].weight,
			op_sla_info[CELLULAR_INDEX].weight);
	}
}

static void change_weight_state_for_small_minute_speed(void)
{
	int index_1 = -1;
	int index_2 = -1;
	int sum_speed = MINUTE_LITTE_RATE;

	if (!op_sla_enable)
		return;

	if (op_sla_info[WLAN_INDEX].if_up &&
	    op_sla_info[CELLULAR_INDEX].if_up) {
		sum_speed = op_sla_info[WLAN_INDEX].minute_speed +
			op_sla_info[CELLULAR_INDEX].minute_speed;

		if (op_sla_info[WLAN_INDEX].weight_state ==
		    WEIGHT_STATE_USELESS) {
			index_1 = WLAN_INDEX;
		}

		if (op_sla_info[CELLULAR_INDEX].weight_state ==
		    WEIGHT_STATE_USELESS) {
			index_2 = CELLULAR_INDEX;
		}

		if ((index_1 != -1 || index_2 != -1) &&
		    sum_speed < MINUTE_LITTE_RATE) {
			if (index_1 != -1) {
				if (op_sla_debug) {
					pr_info("[op_sla] %s: reset index_1:%d\n",
						__func__, index_1);
				}
				op_sla_info[index_1].syn_retran = 0;
				op_sla_info[index_1].weight_state =
					WEIGHT_STATE_RECOVERY;
			}

			if (index_2 != -1) {
				if (op_sla_debug) {
					pr_info("[op_sla] %s: reset index_2:%d\n",
						__func__, index_2);
				}
				op_sla_info[index_2].syn_retran = 0;
				op_sla_info[index_2].weight_state =
					WEIGHT_STATE_RECOVERY;
			}
			op_sla_info[WLAN_INDEX].minute_speed =
				MINUTE_LITTE_RATE;
			op_sla_info[CELLULAR_INDEX].minute_speed =
				MINUTE_LITTE_RATE;
		}
	}
}

static int need_to_recovery_weight(void)
{
	int ret = SLA_WEIGHT_NORMAL;
	int sum_current_speed = 0;
	int index = -1;

	if (!op_sla_enable)
		return ret;

	if (op_sla_info[WLAN_INDEX].weight_state == WEIGHT_STATE_RECOVERY) {
		index = WLAN_INDEX;
	} else if (op_sla_info[CELLULAR_INDEX].weight_state ==
		   WEIGHT_STATE_RECOVERY) {
		index = CELLULAR_INDEX;
	}

	if (index != -1) {
		sum_current_speed = op_sla_info[WLAN_INDEX].current_speed +
			op_sla_info[CELLULAR_INDEX].current_speed;
		sum_current_speed *= 8; //kbit/s
		if (sum_current_speed > MINUTE_LITTE_RATE) {
			sla_write_lock();
			op_sla_info[WLAN_INDEX].sla_avg_rtt = 0;
			op_sla_info[CELLULAR_INDEX].sla_avg_rtt = 0;
			op_sla_info[WLAN_INDEX].weight = 0;
			op_sla_info[CELLULAR_INDEX].weight = 0;
			sla_write_unlock();
			ret = SLA_WEIGHT_RECOVERY;
			if (op_sla_debug)
				pr_info("[op_sla] %s\n", __func__);
		}
	}
	return ret;
}

static void disable_cellular_by_timer(struct timeval tv)
{
	if (op_sla_info[CELLULAR_INDEX].need_disable &&
	    (tv.tv_sec - disable_cellular_tv.tv_sec) >= 10) {
		op_sla_info[CELLULAR_INDEX].need_disable = false;
		op_sla_send_to_user(SLA_DISABLE, NULL, 0);
		if (op_sla_debug)
			pr_info("[op_sla] %s: speed good, disable sla\n",
				__func__);
	}
}

static void up_wlan_iface_by_timer(struct timeval tv)
{
	if (op_sla_info[WLAN_INDEX].need_up &&
	    (tv.tv_sec - calc_wlan_rtt_tv.tv_sec) >= 10) {
		op_sla_info[WLAN_INDEX].if_up = 1;
		op_sla_info[WLAN_INDEX].need_up = false;
	}
}

static void enable_to_user_time_out(struct timeval tv)
{
	if (enable_to_user &&
	    !op_sla_enable &&
	    (tv.tv_sec - last_enable_to_user_tv.tv_sec) >=
	    ENABLE_TO_USER_TIMEOUT) {
		enable_to_user = 0;
		if (op_sla_debug)
			pr_info("[op_sla] %s: enable_to_user timeout\n",
				__func__);
	}
}

static void calc_network_congestion(struct timeval tv)
{
	int index = 0;
	int avg_rtt = 0;

	if (op_sla_debug) {
		pr_info("[op_sla] %s: screen_on:%d enable_to_user:%d\n",
			__func__, sla_screen_on, enable_to_user);
		pr_info("[op_sla] %s: sla_switch_enable:%d op_sla_enable:%d\n",
			__func__, sla_switch_enable, op_sla_enable);
		pr_info("[op_sla] %s: op_sla_def_net:%d\n",
			__func__, op_sla_def_net);
	}
	sla_rtt_write_lock();
	for (index = 0; index < IFACE_NUM; index++) {
		avg_rtt = 0;
		if (op_sla_info[index].if_up || op_sla_info[index].need_up) {
			if (op_sla_info[index].rtt_index >= RTT_NUM) {
				avg_rtt = op_sla_info[index].sum_rtt /
					op_sla_info[index].rtt_index;
				op_sla_info[index].sla_rtt_num++;
				op_sla_info[index].sla_sum_rtt += avg_rtt;
				op_sla_info[index].avg_rtt =
					(7 * op_sla_info[index].avg_rtt +
					avg_rtt) / 8;
				op_sla_info[index].sum_rtt = 0;
				op_sla_info[index].rtt_index = 0;
			}

			if (op_sla_debug) {
				pr_info("[op_sla] %s: index:%d cur_avg_rtt:%d\n",
					__func__, index, avg_rtt);
				pr_info("[op_sla] %s: sum_avg_rtt:%d sla_avg_rtt:%d\n",
					__func__, op_sla_info[index].avg_rtt,
					op_sla_info[index].sla_avg_rtt);
				pr_info("[op_sla] %s: wlan score:%d bad count:%d speed:%d\n",
					__func__, op_sla_info[index].wlan_score,
					op_sla_info[index].wlan_score_bad_count,
					op_sla_info[index].max_speed);
				pr_info("[op_sla] %s: download_flag = %d\n",
					__func__,
					op_sla_info[index].download_flag);
			}
			avg_rtt = op_sla_info[index].sla_avg_rtt;

			if (sla_screen_on && !enable_to_user &&
			    !op_sla_enable && sla_switch_enable &&
			    index == WLAN_INDEX &&
			    ((op_sla_info[index].wlan_score &&
			    op_sla_info[index].wlan_score <=
			    (sla_params_info.wlan_bad_score - 4)) ||
			    (op_sla_info[index].max_speed <=
			    sla_params_info.sla_speed &&
			    op_sla_info[index].download_flag !=
			    DOWN_LOAD_FLAG && (avg_rtt >=
			    sla_params_info.sla_rtt ||
			    op_sla_info[index].wlan_score_bad_count >=
			    WLAN_SCORE_BAD_NUM)))) {
				if (op_sla_debug)
					pr_info("[op_sla] %s: send SLA_ENABLE\n",
						__func__);
				prepare_enable_sla();
				reset_wlan_info();
			}
		}
	}
	sla_rtt_write_unlock();
}

static void init_game_online_info(void)
{
	int i = 0;
	u32 time_now = 0;
	int total = op_sla_game_app_list.count + GAME_BASE;

	if (op_sla_debug)
		pr_info("[op_sla] %s\n", __func__);
	time_now = ktime_get_ns() / 1000000;
	sla_game_write_lock();
	for (i = 0 + GAME_BASE; i < total; i++) {
		op_sla_game_app_list.mark[i] = WLAN_MARK;
		op_sla_game_app_list.switch_time[i] = time_now;
		op_sla_game_app_list.switch_count[i] = 0;
		op_sla_game_app_list.repeat_switch_time[i] = 0;
	}
	sla_game_write_unlock();
	memset(&game_online_info, 0x0, sizeof(struct op_game_online));
}

static void op_sla_timer_function(void)
{
	int time_interval;
	int ret = need_to_recovery_weight();
	struct timeval tv;

	do_gettimeofday(&tv);

	time_interval = tv.tv_sec - last_speed_tv.tv_sec;

	if (time_interval >= CALC_DEV_SPEED_TIME) {
		last_speed_tv = tv;
		change_weight_state();
		back_iface_speed();
		statistic_dev_speed(tv, time_interval);
		calc_network_congestion(tv);
		change_weight_state_for_small_minute_speed();
	}

	if (ret != SLA_WEIGHT_RECOVERY) {
		time_interval = tv.tv_sec - last_weight_tv.tv_sec;
		if (time_interval >= RECALC_WEIGHT_TIME) {
			last_weight_tv = tv;
			detect_network_is_available();
			recalc_dev_weight();
		}
	}

	enable_to_user_time_out(tv);
	up_wlan_iface_by_timer(tv);
	disable_cellular_by_timer(tv);
	reset_op_sla_calc_speed(tv);
	mod_timer(&sla_timer, jiffies + SLA_TIMER_EXPIRES);
}

static void op_sla_timer_init(void)
{
	init_timer(&sla_timer);
	sla_timer.function = (void *)op_sla_timer_function;
	sla_timer.expires = jiffies + SLA_TIMER_EXPIRES;// timer expires in ~1s
	add_timer(&sla_timer);

	do_gettimeofday(&last_speed_tv);
	do_gettimeofday(&last_weight_tv);
	do_gettimeofday(&last_minute_speed_tv);
}

static void op_sla_timer_deinit(void)
{
	del_timer(&sla_timer);
}

static struct nf_hook_ops op_sla_ops[] __read_mostly = {
	{
		.hook		= op_sla,
		.pf		= NFPROTO_IPV4,
		.hooknum	= NF_INET_LOCAL_OUT,
		.priority	= NF_IP_PRI_CONNTRACK + 1,
	},
	{
		.hook		= op_sla_speed_calc,
		.pf		= NFPROTO_IPV4,
		.hooknum	= NF_INET_PRE_ROUTING,
		.priority	= NF_IP_PRI_MANGLE + 1,
	},
	{
		.hook		= op_sla_rx_calc,
		.pf		= NFPROTO_IPV4,
		.hooknum	= NF_INET_LOCAL_IN,
		.priority	= NF_IP_PRI_FILTER + 1,
	},

	{ }
};

static struct ctl_table op_sla_sysctl_table[] = {
	{
		.procname	= "op_sla_enable",
		.data		= &op_sla_enable,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "op_sla_debug",
		.data		= &op_sla_debug,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "op_sla_calc_speed",
		.data		= &op_sla_calc_speed,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "op_sla_rtt_detect",
		.data		= &op_sla_rtt_detect,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname       = "op_sla_link_turbo",
		.data           = &op_sla_link_turbo,
		.maxlen         = sizeof(int),
		.mode           = 0644,
		.proc_handler   = proc_dointvec,
	},
	{ }
};

static int op_sla_sysctl_init(void)
{
	op_sla_table_hrd = register_net_sysctl(&init_net, "net/op_sla",
					       op_sla_sysctl_table);
	return !op_sla_table_hrd ? -ENOMEM : 0;
}

static void op_sla_send_white_list_app_traffic(void)
{
	char *p = NULL;
	char send_msg[1284];

	memset(send_msg, 0x0, sizeof(send_msg));

	p = send_msg;
	memcpy(p, &white_app_list.count, sizeof(u32));

	p += sizeof(u32);
	memcpy(p, white_app_list.uid, WHITE_APP_NUM * sizeof(u32));

	p += WHITE_APP_NUM * sizeof(u32);
	memcpy(p, white_app_list.cell_bytes, WHITE_APP_NUM * sizeof(u64));

	p += WHITE_APP_NUM * sizeof(u64);
	memcpy(p, white_app_list.cell_bytes_normal,
	       WHITE_APP_NUM * sizeof(u64));

	op_sla_send_to_user(SLA_SEND_APP_TRAFFIC,
			    send_msg, sizeof(send_msg));
}

static int op_sla_set_debug(struct nlmsghdr *nlh)
{
	op_sla_debug = *(u32 *)NLMSG_DATA(nlh);
	if (op_sla_debug)
		pr_info("[op_sla] %s: set debug = %d\n",
			__func__, op_sla_debug);
	return	0;
}

static int op_sla_set_game_mark(struct nlmsghdr *nlh)
{
	fw_set_game_mark = *(u32 *)NLMSG_DATA(nlh);
	if (op_sla_debug)
		pr_info("[op_sla] %s: game mark= %d\n",
			__func__, fw_set_game_mark);
	return  0;
}

static int op_sla_set_default_network(struct nlmsghdr *nlh)
{
	op_sla_def_net = *(u32 *)NLMSG_DATA(nlh);
	if (op_sla_debug)
		pr_info("[op_sla] %s: set default network = %d\n", __func__,
			op_sla_def_net);
	return 0;
}

static void op_sla_send_syn_retran_info(void)
{
	op_sla_send_to_user(SLA_GET_SYN_RETRAN_INFO,
			    (char *)&syn_retran_statistic,
			    sizeof(struct op_syn_retran_statistic));
	memset(&syn_retran_statistic, 0x0,
	       sizeof(struct op_syn_retran_statistic));
}

static int disable_op_sla_module(void)
{
	if (op_sla_debug)
		pr_info("[op_sla] %s: op_sla_enable=%d\n",
			__func__, op_sla_enable);
	sla_write_lock();
	if (op_sla_enable) {
		reset_wlan_info();
		enable_to_user = 0;
		op_sla_enable = 0;
		init_game_online_info();
		op_sla_send_to_user(SLA_DISABLED, NULL, 0);
	}
	sla_write_unlock();
	return 0;
}

static int op_sla_iface_up(struct nlmsghdr *nlh)
{
	int index = -1;
	u32 mark = 0x0;
	struct op_dev_info *node = NULL;
	char *p = (char *)NLMSG_DATA(nlh);

	sla_write_lock();

	if (op_sla_debug)
		pr_info("[op_sla] %s: enter type=%d enable_to_user = %d\n",
			__func__, nlh->nlmsg_type, enable_to_user);

	if (nlh->nlmsg_type == SLA_WIFI_UP) {
		mark = WLAN_MARK;
		index = WLAN_INDEX;

		do_gettimeofday(&last_speed_tv);
		do_gettimeofday(&last_weight_tv);
		do_gettimeofday(&last_minute_speed_tv);
		do_gettimeofday(&calc_wlan_rtt_tv);

		op_sla_info[WLAN_INDEX].if_up = 0;
		op_sla_info[WLAN_INDEX].need_up = true;
		op_sla_info[CELLULAR_INDEX].max_speed = 0;
	} else if (nlh->nlmsg_type == SLA_CELLULAR_UP) {
		if (!enable_to_user) {
			op_sla_info[CELLULAR_INDEX].if_up = 0;
			op_sla_info[CELLULAR_INDEX].need_up = true;
		} else {
			op_sla_info[CELLULAR_INDEX].if_up = 1;
		}
		mark = CELLULAR_MARK;
		index = CELLULAR_INDEX;
	}

	if (index != -1 && p) {
		node = &op_sla_info[index];
		node->mark = mark;
		node->minute_speed = MINUTE_LITTE_RATE;
		memcpy(node->dev_name, p, IFACE_LEN);
		if (op_sla_debug)
			pr_info("[op_sla] %s: ifname = %s ifup = %d\n",
				__func__, node->dev_name, node->if_up);
	}
	sla_write_unlock();
	return 0;
}

static int op_sla_iface_down(struct nlmsghdr *nlh)
{
	int index = -1;

	if (op_sla_debug)
		pr_info("[op_sla] %s: type=%d\n", __func__, nlh->nlmsg_type);

	if (nlh->nlmsg_type == SLA_WIFI_DOWN) {
		index = WLAN_INDEX;
		enable_to_user = 0;
		op_sla_calc_speed = 0;
	} else if (nlh->nlmsg_type == SLA_CELLULAR_DOWN) {
		index = CELLULAR_INDEX;
	}

	if (index != -1) {
		sla_write_lock();
		memset(&op_speed_info[index], 0x0,
		       sizeof(struct op_speed_calc));
		memset(&op_sla_info[index], 0x0,
		       sizeof(struct op_dev_info));
		sla_write_unlock();
	}
	return 0;
}

static int op_sla_get_pid(struct sk_buff *skb, struct nlmsghdr *nlh)
{
	op_sla_pid = NETLINK_CB(skb).portid;
	if (op_sla_debug)
		pr_info("[op_sla] %s: op_sla_pid = %u\n", __func__, op_sla_pid);
	return 0;
}

static int op_sla_update_wlan_score(struct nlmsghdr *nlh)
{
	int *score = (int *)NLMSG_DATA(nlh);

	if (op_sla_debug)
		pr_info("[op_sla] %s: score=%d\n", __func__, *score);
	//when wlan+ not be enabled
	if (*score <= (sla_params_info.wlan_bad_score - 4)) {
		if (sla_screen_on &&
		    !enable_to_user &&
		    !op_sla_enable &&
		    sla_switch_enable &&
		    !op_sla_info[CELLULAR_INDEX].need_up &&
		    !op_sla_info[CELLULAR_INDEX].if_up) {
			if (op_sla_debug)
				pr_info("[op_sla] %s: send SLA_ENABLE\n",
					__func__);
			prepare_enable_sla();
			reset_wlan_info();
			op_sla_send_to_user(SLA_ENABLE, NULL, 0);
		}
	}

	op_sla_info[WLAN_INDEX].wlan_score = *score;

	if (*score <= (sla_params_info.wlan_bad_score - 5)) {
		op_sla_info[WLAN_INDEX].wlan_score_bad_count +=
			WLAN_SCORE_BAD_NUM;
	} else if (*score <= (sla_params_info.wlan_bad_score - 2)) {
		op_sla_info[WLAN_INDEX].wlan_score_bad_count += 2;
	} else if (*score <= sla_params_info.wlan_bad_score) {
		op_sla_info[WLAN_INDEX].wlan_score_bad_count++;
	} else if (*score >= sla_params_info.wlan_good_score) {
		op_sla_info[WLAN_INDEX].wlan_score_bad_count = 0;
	} else if (*score >= (sla_params_info.wlan_good_score - 2) &&
		op_sla_info[WLAN_INDEX].wlan_score_bad_count >= 2) {
		op_sla_info[WLAN_INDEX].wlan_score_bad_count -= 2;
	} else if (*score >= (sla_params_info.wlan_good_score - 5) &&
		   op_sla_info[WLAN_INDEX].wlan_score_bad_count) {
		op_sla_info[WLAN_INDEX].wlan_score_bad_count--;
	}

	if (op_sla_info[WLAN_INDEX].wlan_score_bad_count >
	    (2 * WLAN_SCORE_BAD_NUM)) {
		op_sla_info[WLAN_INDEX].wlan_score_bad_count =
			2 * WLAN_SCORE_BAD_NUM;
	}
	return 0;
}

static int op_sla_set_game_app_uid(struct nlmsghdr *nlh)
{
	int i;
	u32 *info = (u32 *)NLMSG_DATA(nlh);
	int total;

	memset(&op_sla_game_app_list, 0x0, sizeof(struct op_game_app_info));
	init_rtt_queue_info();
	op_sla_game_app_list.count = info[0];
	total = op_sla_game_app_list.count + GAME_BASE;
	if (op_sla_game_app_list.count > 0 &&
	    op_sla_game_app_list.count <= GAME_NUM) {
		for (i = 0 + GAME_BASE; i < total; i++) {
			op_sla_game_app_list.uid[i] = info[i];
			op_sla_game_app_list.switch_time[i] = 0;
			op_sla_game_app_list.switch_count[i] = 0;
			op_sla_game_app_list.repeat_switch_time[i] = 0;
			op_sla_game_app_list.game_type[i] = i;
			op_sla_game_app_list.mark[i] = WLAN_MARK;
			if (op_sla_debug)
				pr_info("[op_sla] %s: index=%d uid=%d\n",
					__func__,
					op_sla_game_app_list.game_type[i],
					op_sla_game_app_list.uid[i]);
		}
	}
	return 0;
}

static int op_sla_set_white_list_app_uid(struct nlmsghdr *nlh)
{
	int i;
	u32 *info = (u32 *)NLMSG_DATA(nlh);

	memset(&white_app_list, 0x0, sizeof(struct op_white_app_info));
	white_app_list.count = info[0];
	if (white_app_list.count > 0 && white_app_list.count < WHITE_APP_NUM) {
		for (i = 0; i < white_app_list.count; i++) {
			white_app_list.uid[i] = info[i + 1];
			if (op_sla_debug)
				pr_info("[op_sla] %s: count=%d, uid[%d]=%d\n",
					__func__, white_app_list.count, i,
					white_app_list.uid[i]);
		}
	}
	return 0;
}

static int op_sla_set_netlink_valid(struct nlmsghdr *nlh)
{
	u32 *info = (u32 *)NLMSG_DATA(nlh);

	op_sla_info[WLAN_INDEX].netlink_valid = info[0];
	op_sla_info[CELLULAR_INDEX].netlink_valid = info[1];
	if (op_sla_debug)
		pr_info("[op_sla] %s: wlan valid:%d cell valid:%d\n",
			__func__,
			op_sla_info[WLAN_INDEX].netlink_valid,
			op_sla_info[CELLULAR_INDEX].netlink_valid);
	return 0;
}

static int op_sla_set_game_rtt_detecting(struct nlmsghdr *nlh)
{
	op_sla_rtt_detect = (nlh->nlmsg_type == SLA_ENABLE_GAME_RTT) ? 1 : 0;
	if (op_sla_debug)
		pr_info("[op_sla] %s: set game rtt detect:%d\n", __func__,
			op_sla_rtt_detect);
	return 0;
}

static int op_sla_enable_link_turbo(struct nlmsghdr *nlh)
{
	op_sla_link_turbo = (nlh->nlmsg_type == SLA_ENABLE_LINK_TURBO) ? 1 : 0;
	if (op_sla_debug)
		pr_info("[op_sla] %s: enable link turbo:%d\n", __func__,
			op_sla_link_turbo);
	return 0;
}

static int op_sla_set_switch_state(struct nlmsghdr *nlh)
{
	u32 *switch_enable = (u32 *)NLMSG_DATA(nlh);

	sla_switch_enable = *switch_enable;
	if (op_sla_debug)
		pr_info("[op_sla] %s: sla switch:%d\n",
			__func__, sla_switch_enable);
	return 0;
}

static int op_sla_update_screen_state(struct nlmsghdr *nlh)
{
	u32 *screen_state = (u32 *)NLMSG_DATA(nlh);

	sla_screen_on =	(*screen_state)	? true : false;
	if (op_sla_debug) {
		pr_info("[op_sla] %s: update screen state = %u\n", __func__,
			sla_screen_on);
	}
	return	0;
}

static int op_sla_update_cell_score(struct nlmsghdr *nlh)
{
	int *score = (int *)NLMSG_DATA(nlh);

	op_sla_info[CELLULAR_INDEX].cell_score = *score;

	if (op_sla_debug) {
		pr_info("[op_sla] %s: update cell score:%d\n", __func__,
			op_sla_info[CELLULAR_INDEX].cell_score);
	}
	return	0;
}

static int op_sla_set_show_dialog_state(struct nlmsghdr *nlh)
{
	u32 *window_state = (u32 *)NLMSG_DATA(nlh);

	need_pop_window = (*window_state) ? true : false;
	if (op_sla_debug) {
		pr_info("[op_sla] %s: set show dialog = %u\n", __func__,
			need_pop_window);
	}
	return	0;
}

//TODO: not use
static int op_sla_set_params(struct nlmsghdr *nlh)
{
	u32 *params = (u32 *)NLMSG_DATA(nlh);
	u32 count = params[0];

	params++;
	if (count == 9) {
		sla_params_info.sla_speed = params[0];
		sla_params_info.cell_speed = params[1];
		sla_params_info.wlan_speed = params[2];
		sla_params_info.wlan_little_score_speed = params[3];
		sla_params_info.sla_rtt = params[4];
		sla_params_info.wzry_rtt = params[5];
		sla_params_info.cjzc_rtt = params[6];
		sla_params_info.pubg_rtt = params[7];
		sla_params_info.qqcar_rtt = params[8];
		sla_params_info.wlan_bad_score = params[9];
		sla_params_info.wlan_good_score = params[10];
	} else {
		if (op_sla_debug) {
			pr_info("[op_sla] %s: set params invalid param count:%d\n",
				__func__, count);
		}
	}

	return	0;
}

static int op_sla_set_game_start_state(struct nlmsghdr *nlh)
{
	int *data = (int *)NLMSG_DATA(nlh);

	game_start_state = *data;
	if (op_sla_debug) {
		pr_info("[op_sla] %s: set game_start_state = %d\n", __func__,
			game_start_state);
	}
	return	0;
}

static int sla_netlink_rcv_msg(struct sk_buff *skb, struct nlmsghdr *nlh)
{
	int ret = 0;

	switch (nlh->nlmsg_type) {
	case SLA_ENABLE:
		ret = enable_op_sla_module();
		break;
	case SLA_DISABLE:
		ret = disable_op_sla_module();
		break;
	case SLA_WIFI_UP:
	case SLA_CELLULAR_UP:
		ret = op_sla_iface_up(nlh);
		break;
	case SLA_WIFI_DOWN:
	case SLA_CELLULAR_DOWN:
		ret = op_sla_iface_down(nlh);
		break;
	case SLA_NOTIFY_PID:
		ret = op_sla_get_pid(skb, nlh);
		break;
	case SLA_NOTIFY_WIFI_SCORE:
		ret = op_sla_update_wlan_score(nlh);
		break;
	case SLA_NOTIFY_GAME_UID:
		ret = op_sla_set_game_app_uid(nlh);
		break;
	case SLA_NOTIFY_WHITE_LIST_APP:
		//op_sla_send_white_list_app_traffic();
		ret = op_sla_set_white_list_app_uid(nlh);
		break;
	case SLA_SET_NETWORK_VALID:
		ret = op_sla_set_netlink_valid(nlh);
		break;
	case SLA_ENABLE_GAME_RTT:
	case SLA_DISABLE_GAME_RTT:
		ret = op_sla_set_game_rtt_detecting(nlh);
		break;
	case SLA_ENABLE_LINK_TURBO:
	case SLA_DISABLE_LINK_TURBO:
		ret = op_sla_enable_link_turbo(nlh);
		break;
	case SLA_NOTIFY_SWITCH_STATE:
		ret = op_sla_set_switch_state(nlh);
		break;
	case SLA_NOTIFY_SCREEN_STATE:
		ret = op_sla_update_screen_state(nlh);
		break;
	case SLA_NOTIFY_CELL_SCORE:
		ret = op_sla_update_cell_score(nlh);
		break;
	case SLA_NOTIFY_SHOW_DIALOG:
		ret = op_sla_set_show_dialog_state(nlh);
		break;
	case SLA_GET_SYN_RETRAN_INFO:
		op_sla_send_syn_retran_info();
		break;
	case SLA_GET_SPEED_UP_APP:
		op_sla_send_white_list_app_traffic();
		break;
	case SLA_SET_DEBUG:
		op_sla_set_debug(nlh);
		break;
	case SLA_SET_GAME_MARK:
		op_sla_set_game_mark(nlh);
		break;
	case SLA_NOTIFY_DEFAULT_NETWORK:
		op_sla_set_default_network(nlh);
		break;
	case SLA_NOTIFY_PARAMS:
		op_sla_set_params(nlh);
		break;
	case SLA_NOTIFY_GAME_STATE:
		op_sla_set_game_start_state(nlh);
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static void sla_netlink_rcv(struct sk_buff *skb)
{
	mutex_lock(&sla_netlink_mutex);
	netlink_rcv_skb(skb, &sla_netlink_rcv_msg);
	mutex_unlock(&sla_netlink_mutex);
}

static int op_sla_netlink_init(void)
{
	struct netlink_kernel_cfg cfg = {
		.input	= sla_netlink_rcv,
	};

	op_sla_sock = netlink_kernel_create(&init_net, NETLINK_OP_SLA, &cfg);
	return !op_sla_sock ? -ENOMEM : 0;
}

static void op_sla_netlink_exit(void)
{
	netlink_kernel_release(op_sla_sock);
	op_sla_sock = NULL;
}

static void init_parameter(void)
{
	op_sla_rtt_detect = 1;
	op_sla_link_turbo = 0;
	op_sla_debug = 0;
	fw_set_game_mark = -1;
	op_sla_def_net = 0;    //WLAN->0 CELL->1
	game_start_state = 0;
	game_rtt_wan_detect_flag = 0;
	sla_switch_enable = false;
	sla_screen_on = true;
	need_pop_window = false;
	rtt_rear = 0;
}

static int __init op_sla_init(void)
{
	int ret = 0;

	init_parameter();
	rwlock_init(&sla_lock);
	rwlock_init(&sla_rtt_lock);
	rwlock_init(&sla_game_lock);
	rwlock_init(&sla_game_rx_lock);

	ret = op_sla_netlink_init();
	if (ret < 0) {
		pr_info("[op_sla] %s: module can not init op sla netlink.\n",
			__func__);
	}

	ret |= op_sla_sysctl_init();

	ret |= nf_register_net_hooks(&init_net,
		op_sla_ops, ARRAY_SIZE(op_sla_ops));
	if (ret < 0) {
		pr_info("[op_sla] %s: module can not register netfilter ops.\n",
			__func__);
	}

	op_sla_timer_init();
	statistic_dev_rtt = op_statistic_dev_rtt;

	return ret;
}

static void __exit op_sla_deinit(void)
{
	op_sla_timer_deinit();
	statistic_dev_rtt = NULL;
	op_sla_netlink_exit();

	if (op_sla_table_hrd)
		unregister_net_sysctl_table(op_sla_table_hrd);

	nf_unregister_net_hooks(&init_net, op_sla_ops, ARRAY_SIZE(op_sla_ops));
}

module_init(op_sla_init);
module_exit(op_sla_deinit);
