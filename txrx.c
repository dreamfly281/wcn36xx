/*
 * Contact: Eugene Krasnikov <k.eugene.e@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "txrx.h"

#define RSSI0(x) (100 - ((x->phy_stat0 >> 24) & 0xff))

int wcn36xx_rx_skb(struct wcn36xx *wcn, struct sk_buff *skb)
{
	struct ieee80211_rx_status status;
	struct ieee80211_hdr *hdr;
	struct wcn36xx_rx_bd *bd;
	u16 fc, sn;
	/*
	 * All fields must be 0, otherwise it can lead to
	 * unexpected consequences.
	 */
	memset(&status, 0, sizeof(status));

	bd = (struct wcn36xx_rx_bd *)skb->data;
	buff_to_be((u32 *)bd, sizeof(*bd)/sizeof(u32));

	skb_put(skb, bd->pdu.mpdu_header_off + bd->pdu.mpdu_len);
	skb_pull(skb, bd->pdu.mpdu_header_off);

	status.mactime = 10;
	status.freq = wcn->current_channel->center_freq;
	status.band = wcn->current_channel->band;
	status.signal = -RSSI0(bd);
	status.antenna = 1;
	status.rate_idx = 1;
	status.flag = 0;
	status.rx_flags = 0;
	status.flag |= RX_FLAG_IV_STRIPPED |
		       RX_FLAG_MMIC_STRIPPED |
		       RX_FLAG_DECRYPTED;
	wcn36xx_dbg(WCN36XX_DBG_RX, "status.flags=%x "
		    "status->vendor_radiotap_len=%x",
		    status.flag,  status.vendor_radiotap_len);

	memcpy(IEEE80211_SKB_RXCB(skb), &status, sizeof(status));

	hdr = (struct ieee80211_hdr *) skb->data;
	fc = __le16_to_cpu(hdr->frame_control);
	sn = IEEE80211_SEQ_TO_SN(__le16_to_cpu(hdr->seq_ctrl));

	if (ieee80211_is_beacon(hdr->frame_control)) {
		wcn36xx_dbg(WCN36XX_DBG_BEACON, "beacon skb %p len %d fc %04x sn %d",
			    skb, skb->len, fc, sn);
		wcn36xx_dbg_dump(WCN36XX_DBG_BEACON_DUMP, "SKB <<< ",
				 (char *)skb->data, skb->len);
	} else {
		wcn36xx_dbg(WCN36XX_DBG_RX, "rx skb %p len %d fc %04x sn %d",
			    skb, skb->len, fc, sn);
		wcn36xx_dbg_dump(WCN36XX_DBG_RX_DUMP, "SKB <<< ",
				 (char *)skb->data, skb->len);
	}

	ieee80211_rx_ni(wcn->hw, skb);

	return 0;
}

static void wcn36xx_set_tx_pdu(struct wcn36xx_tx_bd *bd,
			 u32 mpdu_header_len,
			 u32 len)
{
	bd->pdu.mpdu_header_len = mpdu_header_len;
	bd->pdu.mpdu_header_off = sizeof(*bd);
	bd->pdu.mpdu_data_off = bd->pdu.mpdu_header_len +
		bd->pdu.mpdu_header_off;
	bd->pdu.mpdu_len = len;
	bd->pdu.tid = WCN36XX_TID;
}

static void wcn36xx_set_tx_data(struct wcn36xx_tx_bd *bd,
				struct wcn36xx *wcn,
				struct wcn_sta *sta_priv,
				struct ieee80211_hdr *hdr,
				bool bcast)
{
	bd->bd_rate = WCN36XX_BD_RATE_DATA;
	bd->dpu_sign = wcn->current_vif->ucast_dpu_signature;
	bd->sta_index = wcn->current_vif->sta_index;
	bd->dpu_desc_idx = wcn->current_vif->dpu_desc_index;
	if (ieee80211_is_nullfunc(hdr->frame_control) ||
	   (sta_priv && !sta_priv->is_data_encrypted))
		bd->dpu_ne = 1;
	if (bcast) {
		bd->ub = 1;
		bd->ack_policy = 1;
	}
}

static void wcn36xx_set_tx_mgmt(struct wcn36xx_tx_bd *bd,
				struct wcn36xx *wcn,
				struct ieee80211_hdr *hdr,
				bool bcast)
{
	bd->sta_index = wcn->current_vif->self_sta_index;
	bd->dpu_desc_idx = wcn->current_vif->self_dpu_desc_index;
	bd->dpu_ne = 1;

	/* default rate for unicast */
	if (ieee80211_is_mgmt(hdr->frame_control))
		bd->bd_rate = (wcn->band == IEEE80211_BAND_5GHZ) ?
			WCN36XX_BD_RATE_CTRL :
			WCN36XX_BD_RATE_MGMT;
	else if (ieee80211_is_ctl(hdr->frame_control))
		bd->bd_rate = WCN36XX_BD_RATE_CTRL;
	else
		wcn36xx_warn("frame control type unknown");

	/*
	 * In joining state trick hardware that probe is sent as
	 * unicast even if address is broadcast.
	 */
	if (wcn->is_joining &&
	    ieee80211_is_probe_req(hdr->frame_control))
		bcast = false;

	if (bcast) {
		/* broadcast */
		bd->ub = 1;
		/* No ack needed not unicast */
		bd->ack_policy = 1;
		bd->queue_id = WCN36XX_TX_B_WQ_ID;
	} else
		bd->queue_id = WCN36XX_TX_U_WQ_ID;
}

int wcn36xx_start_tx(struct wcn36xx *wcn,
		     struct wcn_sta *sta_priv,
		     struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	unsigned long flags;
	bool is_low = ieee80211_is_data(hdr->frame_control);
	bool bcast = is_broadcast_ether_addr(hdr->addr1) ||
		is_multicast_ether_addr(hdr->addr1);
	struct wcn36xx_tx_bd *bd = wcn36xx_dxe_get_next_bd(wcn, is_low);

	if (!bd) {
		/*
		 * TX DXE are used in pairs. One for the BD and one for the
		 * actual frame. The BD DXE's has a preallocated buffer while
		 * the skb ones does not. If this isn't true something is really
		 * wierd. TODO: Recover from this situation
		 */

		wcn36xx_error("bd address may not be NULL for BD DXE");
		return -EINVAL;
	}
	memset(bd, 0, sizeof(*bd));

	wcn36xx_dbg(WCN36XX_DBG_TX,
		    "tx skb %p len %d fc %04x sn %d %s %s",
		    skb, skb->len, __le16_to_cpu(hdr->frame_control),
		    IEEE80211_SEQ_TO_SN(__le16_to_cpu(hdr->seq_ctrl)),
		    is_low ? "low" : "high", bcast ? "bcast" : "ucast");

	wcn36xx_dbg_dump(WCN36XX_DBG_TX_DUMP, "", skb->data, skb->len);

	bd->dpu_rf = WCN36XX_BMU_WQ_TX;

	bd->tx_comp = info->flags & IEEE80211_TX_CTL_REQ_TX_STATUS;
	if (bd->tx_comp) {
		wcn36xx_dbg(WCN36XX_DBG_DXE, "TX_ACK status requested");
		spin_lock_irqsave(&wcn->dxe_lock, flags);
		if (wcn->tx_ack_skb) {
			spin_unlock_irqrestore(&wcn->dxe_lock, flags);
			wcn36xx_warn("tx_ack_skb already set");
			ieee80211_free_txskb(wcn->hw, skb);
			return -EINVAL;
		}

		wcn->tx_ack_skb = skb;
		spin_unlock_irqrestore(&wcn->dxe_lock, flags);

		/* Only one at a time is supported by fw. Stop the TX queues
		 * until the ack status gets back.
		 *
		 * TODO: Add watchdog in case FW does not answer
		 */
		ieee80211_stop_queues(wcn->hw);
	}

	wcn36xx_set_tx_pdu(bd, ieee80211_is_data_qos(hdr->frame_control) ?
			   sizeof(struct ieee80211_qos_hdr) :
			   sizeof(struct ieee80211_hdr_3addr),
			   skb->len);

	/* Data frames served first*/
	if (is_low) {
		wcn36xx_set_tx_data(bd, wcn, sta_priv, hdr, bcast);
	} else {
		/* MGMT and CTRL frames are handeld here*/
		wcn36xx_set_tx_mgmt(bd, wcn, hdr, bcast);
	}

	buff_to_be((u32 *)bd, sizeof(*bd)/sizeof(u32));
	bd->tx_bd_sign = 0xbdbdbdbd;

	return wcn36xx_dxe_tx_frame(wcn, skb, is_low);
}
