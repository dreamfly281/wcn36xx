/*
 * Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
 *
 * Licensed under the ISC license by Qualcomm Atheros, Inc.
 *
 *
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all
 * copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
#include "wcn36xx.h"

static int wcn36xx_enable_keep_alive_null_packet(struct wcn36xx *wcn)
{
	wcn36xx_dbg(WCN36XX_DBG_PMC, "%s", __func__);
	return wcn36xx_smd_keep_alive_req(wcn, WCN36XX_HAL_KEEP_ALIVE_NULL_PKT, 
					  WCN36XX_KEEP_ALIVE_TIME_PERIOD);
}


static int wcn36xx_disable_keep_alive_null_packet(struct wcn36xx *wcn)
{
	return wcn36xx_smd_keep_alive_req(wcn, WCN36XX_HAL_KEEP_ALIVE_NULL_PKT,
					  0);
}

/*
 *
 * @return: 0 means enter BMPS success, other means not enter BMPS mode 
 */
int wcn36xx_pmc_enter_bmps_state(struct wcn36xx *wcn)
{
	struct ieee80211_vif *vif = container_of((void *)wcn->current_vif,
						 struct ieee80211_vif,
						 drv_priv);
	int ret = -1;

	if ((atomic_read(&wcn->full_power_cnt) == 0) && (wcn->aid > 0)) {
		if (!(wcn->pw_state & WCN36XX_BMPS_MODE)) {
			wcn36xx_enable_keep_alive_null_packet(wcn);	
			wcn36xx_smd_enter_bmps(wcn, vif->bss_conf.sync_tsf);
			wcn->pw_state |= WCN36XX_BMPS_MODE;
			ret = 0;			
		}
	}

	return ret;
}

int wcn36xx_pmc_exit_bmps_state(struct wcn36xx *wcn)
{
	if ((atomic_read(&wcn->full_power_cnt) > 0) || (wcn->aid == 0)) {
		if (wcn->pw_state & WCN36XX_BMPS_MODE) {
			wcn36xx_disable_keep_alive_null_packet(wcn);
			wcn36xx_smd_exit_bmps(wcn);
			wcn->pw_state &= ~WCN36XX_BMPS_MODE;
		}
	}
	return 0;
}

/* TODO: For DHCP/roaming protect? */
void wcn36xx_vote_enter_full_power(struct wcn36xx *wcn)
{
	atomic_inc(&wcn->full_power_cnt);
}

/*
 * This fucntion should be called pair with wcn36xx_vote_enter_full_power
 */
void wcn36xx_vote_enter_bmps(struct wcn36xx *wcn)
{
	if (atomic_read(&wcn->full_power_cnt) <= 0)
		wcn36xx_warn("The vote bmps called without paired");
	atomic_dec(&wcn->full_power_cnt);
}

static void wcn36xx_pmc_bmps_measure(u_long data)
{
	struct wcn36xx *wcn = (struct wcn36xx *)data;

	/* Enter or Exit BMPS depend on the currently condition */
	wcn36xx_pmc_enter_bmps_state(wcn);
	wcn36xx_pmc_exit_bmps_state(wcn);
	mod_timer(&wcn->bmps_timer, 
		  jiffies + (BMPS_MEASURE_TIMER_DEFAULT * HZ) / 1000);
}


static void wcn36xx_init_bmps_timer(struct timer_list *bmps_timer, long data)
{
	init_timer(bmps_timer);
	bmps_timer->expires = jiffies + 
		(BMPS_MEASURE_TIMER_DEFAULT * HZ) / 1000;
	bmps_timer->data = data;
	bmps_timer->function = wcn36xx_pmc_bmps_measure;
	add_timer(bmps_timer);
}


int wcn36xx_pmc_init(struct wcn36xx *wcn)
{
	wcn->pw_state = WCN36XX_FULL_POWER;
	atomic_set(&wcn->full_power_cnt, 0);
	wcn36xx_init_bmps_timer(&wcn->bmps_timer, (long)wcn);

	return 0;
}

int wcn36xx_pmc_deinit(struct wcn36xx *wcn)
{
	del_timer(&wcn->bmps_timer);
	return 0;
}
