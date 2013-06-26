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

int wcn36xx_pmu_init(struct wcn36xx *wcn)
{
	wcn->pw_state = WCN36XX_FULL_POWER;
	return 0;
}

int wcn36xx_pmc_enter_bmps_state(struct wcn36xx *wcn, u64 tbtt)
{
	u64 tsf = wcn->vif->bss_conf.sync_tsf;
	/* TODO: Check the TX&RX status */
	/* Disable the TX queue */
	ieee80211_stop_queue(wcn->hw, 0);
	/* Enter the BMPS mode */
	wcn36xx_smd_enter_bmps(wcn, tsf);
	/* Set the enter BMPS status */
	wcn->pw_state = WCN36XX_BMPS;
	return 0;
}

int wcn36xx_pmc_exit_bmps_state(struct wcn36xx *wcn)
{
	/* Exit the BMPS mode */
	wcn36xx_smd_exit_bmps(wcn);
	/* Set the exit BMPS status */
	wcn->pw_state = WCN36XX_FULL_POWER;
	/* Enable the TX queue */
	ieee80211_wake_queue(wcn->hw, 0);
	return 0;
}
