/*
** $Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_3/nic/nic_rx.c#3 $
*/

/*! \file   nic_rx.c
    \brief  Functions that provide many rx-related functions

    This file includes the functions used to process RFB and dispatch RFBs to
    the appropriate related rx functions for protocols.
*/





/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
#include "precomp.h"

#ifndef LINUX
#include <limits.h>
#else
#include <linux/limits.h>
#endif


#include "gl_os.h"
#include "debug.h"
#include "wlan_lib.h"
#include "gl_wext.h"
#include <linux/can/netlink.h>
#include <net/netlink.h>
#include <net/cfg80211.h>
#include "gl_cfg80211.h"
#include "gl_vendor.h"

extern  P_SW_RFB_T g_arGscnResultsTempBuffer[];
extern  UINT_8 g_GscanResultsTempBufferIndex;
extern  UINT_8 g_arGscanResultsIndicateNumber[];
extern  UINT_8 g_GetResultsBufferedCnt;
extern  UINT_8 g_GetResultsCmdCnt;

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/
#define RX_RESPONSE_TIMEOUT (1000)

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/

/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/

/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/

#if CFG_MGMT_FRAME_HANDLING
static PROCESS_RX_MGT_FUNCTION apfnProcessRxMgtFrame[MAX_NUM_OF_FC_SUBTYPES] = {
    #if CFG_SUPPORT_AAA
    aaaFsmRunEventRxAssoc,              /* subtype 0000: Association request */
    #else
    NULL,                               /* subtype 0000: Association request */
    #endif /* CFG_SUPPORT_AAA */
    saaFsmRunEventRxAssoc,              /* subtype 0001: Association response */
    #if CFG_SUPPORT_AAA
    aaaFsmRunEventRxAssoc,              /* subtype 0010: Reassociation request */
    #else
    NULL,                               /* subtype 0010: Reassociation request */
    #endif /* CFG_SUPPORT_AAA */
    saaFsmRunEventRxAssoc,              /* subtype 0011: Reassociation response */
    #if CFG_SUPPORT_ADHOC
    bssProcessProbeRequest,             /* subtype 0100: Probe request */
    #else
    NULL,                               /* subtype 0100: Probe request */
    #endif /* CFG_SUPPORT_ADHOC */
    scanProcessBeaconAndProbeResp,      /* subtype 0101: Probe response */
    NULL,                               /* subtype 0110: reserved */
    NULL,                               /* subtype 0111: reserved */
    scanProcessBeaconAndProbeResp,      /* subtype 1000: Beacon */
    NULL,                               /* subtype 1001: ATIM */
    saaFsmRunEventRxDisassoc,           /* subtype 1010: Disassociation */
    authCheckRxAuthFrameTransSeq,       /* subtype 1011: Authentication */
    saaFsmRunEventRxDeauth,             /* subtype 1100: Deauthentication */
    nicRxProcessActionFrame,            /* subtype 1101: Action */
    NULL,                               /* subtype 1110: reserved */
    NULL                                /* subtype 1111: reserved */
};
#endif


/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

/*******************************************************************************
*                   F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/
/*----------------------------------------------------------------------------*/
/*!
* @brief Initialize the RFBs
*
* @param prAdapter      Pointer to the Adapter structure.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
nicRxInitialize (
    IN P_ADAPTER_T prAdapter
    )
{
    P_RX_CTRL_T prRxCtrl;
    PUINT_8 pucMemHandle;
    P_SW_RFB_T prSwRfb = (P_SW_RFB_T)NULL;
    UINT_32 i;

    DEBUGFUNC("nicRxInitialize");

    ASSERT(prAdapter);
    prRxCtrl = &prAdapter->rRxCtrl;

    //4 <0> Clear allocated memory.
    kalMemZero((PVOID) prRxCtrl->pucRxCached, prRxCtrl->u4RxCachedSize);

    //4 <1> Initialize the RFB lists
    QUEUE_INITIALIZE(&prRxCtrl->rFreeSwRfbList);
    QUEUE_INITIALIZE(&prRxCtrl->rReceivedRfbList);
    QUEUE_INITIALIZE(&prRxCtrl->rIndicatedRfbList);

    pucMemHandle = prRxCtrl->pucRxCached;
    for (i = CFG_RX_MAX_PKT_NUM; i != 0; i--) {
        prSwRfb = (P_SW_RFB_T)pucMemHandle;

        nicRxSetupRFB(prAdapter, prSwRfb);
        nicRxReturnRFB(prAdapter, prSwRfb);

        pucMemHandle += ALIGN_4(sizeof(SW_RFB_T));
    }

    ASSERT(prRxCtrl->rFreeSwRfbList.u4NumElem == CFG_RX_MAX_PKT_NUM);
    /* Check if the memory allocation consist with this initialization function */
    ASSERT((ULONG)(pucMemHandle - prRxCtrl->pucRxCached) == prRxCtrl->u4RxCachedSize);

    //4 <2> Clear all RX counters
    RX_RESET_ALL_CNTS(prRxCtrl);

#if CFG_SDIO_RX_AGG
    prRxCtrl->pucRxCoalescingBufPtr = prAdapter->pucCoalescingBufCached;
    #if !defined(MT5931)
    HAL_CFG_MAX_HIF_RX_LEN_NUM(prAdapter, CFG_SDIO_MAX_RX_AGG_NUM);
    #endif
#else
    #if !defined(MT5931)
    HAL_CFG_MAX_HIF_RX_LEN_NUM(prAdapter, 1);
    #endif
#endif

#if CFG_HIF_STATISTICS
    prRxCtrl->u4TotalRxAccessNum = 0;
    prRxCtrl->u4TotalRxPacketNum = 0;
#endif

#if CFG_HIF_RX_STARVATION_WARNING
    prRxCtrl->u4QueuedCnt = 0;
    prRxCtrl->u4DequeuedCnt = 0;
#endif

    return;
} /* end of nicRxInitialize() */


#if defined(MT5931)
/*----------------------------------------------------------------------------*/
/*!
* @brief Initialize HIF RX control registers explicitly
*
* @param prAdapter      Pointer to the Adapter structure.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
nicRxPostInitialize (
    IN P_ADAPTER_T prAdapter
    )
{
    P_RX_CTRL_T prRxCtrl;
    DEBUGFUNC("nicRxPostInitialize");

    ASSERT(prAdapter);
    prRxCtrl = &prAdapter->rRxCtrl;

#if CFG_SDIO_RX_AGG
    HAL_CFG_MAX_HIF_RX_LEN_NUM(prAdapter, CFG_SDIO_MAX_RX_AGG_NUM);
#else
    HAL_CFG_MAX_HIF_RX_LEN_NUM(prAdapter, 1);
#endif

} /* end of nicRxPostInitialize() */
#endif


/*----------------------------------------------------------------------------*/
/*!
* @brief Uninitialize the RFBs
*
* @param prAdapter      Pointer to the Adapter structure.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
nicRxUninitialize (
    IN P_ADAPTER_T prAdapter
    )
{
    P_RX_CTRL_T prRxCtrl;
    P_SW_RFB_T prSwRfb = (P_SW_RFB_T)NULL;
    KAL_SPIN_LOCK_DECLARATION();

    ASSERT(prAdapter);
    prRxCtrl = &prAdapter->rRxCtrl;
    ASSERT(prRxCtrl);

    nicRxFlush(prAdapter);

    do {
        KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_RX_QUE);
        QUEUE_REMOVE_HEAD(&prRxCtrl->rReceivedRfbList, prSwRfb, P_SW_RFB_T);
        KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_RX_QUE);
        if (prSwRfb){
            if (prSwRfb->pvPacket) {
                kalPacketFree(prAdapter->prGlueInfo, prSwRfb->pvPacket);
            }
            prSwRfb->pvPacket = NULL;
        }
        else {
            break;
        }
    }while (TRUE);

    do {
        KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_RX_QUE);
        QUEUE_REMOVE_HEAD(&prRxCtrl->rFreeSwRfbList, prSwRfb, P_SW_RFB_T);
        KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_RX_QUE);
        if (prSwRfb){
            if (prSwRfb->pvPacket) {
                kalPacketFree(prAdapter->prGlueInfo, prSwRfb->pvPacket);
            }
            prSwRfb->pvPacket = NULL;
        }
        else {
            break;
        }
    }while (TRUE);

    return;
} /* end of nicRxUninitialize() */


/*----------------------------------------------------------------------------*/
/*!
* @brief Fill RFB
*
* @param prAdapter pointer to the Adapter handler
* @param prSWRfb   specify the RFB to receive rx data
*
* @return (none)
*
*/
/*----------------------------------------------------------------------------*/
VOID
nicRxFillRFB (
    IN P_ADAPTER_T    prAdapter,
    IN OUT P_SW_RFB_T prSwRfb
    )
{
    P_HIF_RX_HEADER_T prHifRxHdr;

    UINT_32 u4PktLen = 0;
    UINT_32 u4MacHeaderLen;
    UINT_32 u4HeaderOffset;

    DEBUGFUNC("nicRxFillRFB");

    ASSERT(prAdapter);
    ASSERT(prSwRfb);

    prHifRxHdr = prSwRfb->prHifRxHdr;
    ASSERT(prHifRxHdr);

    u4PktLen= prHifRxHdr->u2PacketLen;

    u4HeaderOffset = (UINT_32)(prHifRxHdr->ucHerderLenOffset & HIF_RX_HDR_HEADER_OFFSET_MASK);
    u4MacHeaderLen = (UINT_32)(prHifRxHdr->ucHerderLenOffset & HIF_RX_HDR_HEADER_LEN)
                    >> HIF_RX_HDR_HEADER_LEN_OFFSET;

    //DBGLOG(RX, TRACE, ("u4HeaderOffset = %d, u4MacHeaderLen = %d\n",
    //    u4HeaderOffset, u4MacHeaderLen));

    prSwRfb->u2HeaderLen = (UINT_16)u4MacHeaderLen;
    prSwRfb->pvHeader = (PUINT_8)prHifRxHdr + HIF_RX_HDR_SIZE + u4HeaderOffset;
    prSwRfb->u2PacketLen = (UINT_16)(u4PktLen - (HIF_RX_HDR_SIZE + u4HeaderOffset));

    //DBGLOG(RX, TRACE, ("Dump Rx packet, u2PacketLen = %d\n", prSwRfb->u2PacketLen));
    //DBGLOG_MEM8(RX, TRACE, prSwRfb->pvHeader, prSwRfb->u2PacketLen);

#if 0
    if (prHifRxHdr->ucReorder & HIF_RX_HDR_80211_HEADER_FORMAT){
        prSwRfb->u4HifRxHdrFlag |= HIF_RX_HDR_FLAG_802_11_FORMAT;
        DBGLOG(RX, TRACE, ("HIF_RX_HDR_FLAG_802_11_FORMAT\n"));
    }

    if (prHifRxHdr->ucReorder & HIF_RX_HDR_DO_REORDER){
        prSwRfb->u4HifRxHdrFlag |= HIF_RX_HDR_FLAG_DO_REORDERING;
        DBGLOG(RX, TRACE, ("HIF_RX_HDR_FLAG_DO_REORDERING\n"));

        /* Get Seq. No and TID, Wlan Index info */
        if (prHifRxHdr->u2SeqNoTid & HIF_RX_HDR_BAR_FRAME){
            prSwRfb->u4HifRxHdrFlag |= HIF_RX_HDR_FLAG_BAR_FRAME;
            DBGLOG(RX, TRACE, ("HIF_RX_HDR_FLAG_BAR_FRAME\n"));
        }

        prSwRfb->u2SSN = prHifRxHdr->u2SeqNoTid & HIF_RX_HDR_SEQ_NO_MASK;
        prSwRfb->ucTid = (UINT_8)((prHifRxHdr->u2SeqNoTid & HIF_RX_HDR_TID_MASK)
                        >> HIF_RX_HDR_TID_OFFSET);
        DBGLOG(RX, TRACE, ("u2SSN = %d, ucTid = %d\n",
            prSwRfb->u2SSN, prSwRfb->ucTid));
    }

    if (prHifRxHdr->ucReorder & HIF_RX_HDR_WDS){
        prSwRfb->u4HifRxHdrFlag |= HIF_RX_HDR_FLAG_AMP_WDS;
        DBGLOG(RX, TRACE, ("HIF_RX_HDR_FLAG_AMP_WDS\n"));
    }
#endif
}


#if CFG_TCP_IP_CHKSUM_OFFLOAD || CFG_TCP_IP_CHKSUM_OFFLOAD_NDIS_60
/*----------------------------------------------------------------------------*/
/*!
* @brief Fill checksum status in RFB
*
* @param prAdapter pointer to the Adapter handler
* @param prSWRfb the RFB to receive rx data
* @param u4TcpUdpIpCksStatus specify the Checksum status
*
* @return (none)
*
*/
/*----------------------------------------------------------------------------*/
VOID
nicRxFillChksumStatus(
    IN  P_ADAPTER_T   prAdapter,
    IN OUT P_SW_RFB_T prSwRfb,
    IN  UINT_32 u4TcpUdpIpCksStatus
)
{

    ASSERT(prAdapter);
    ASSERT(prSwRfb);

    if (prAdapter->u4CSUMFlags != CSUM_NOT_SUPPORTED){
        if (u4TcpUdpIpCksStatus & RX_CS_TYPE_IPv4) { // IPv4 packet
            prSwRfb->aeCSUM[CSUM_TYPE_IPV6] = CSUM_RES_NONE;
            if(u4TcpUdpIpCksStatus & RX_CS_STATUS_IP) { //IP packet csum failed
                prSwRfb->aeCSUM[CSUM_TYPE_IPV4] = CSUM_RES_FAILED;
            } else {
                prSwRfb->aeCSUM[CSUM_TYPE_IPV4] = CSUM_RES_SUCCESS;
            }

            if (u4TcpUdpIpCksStatus & RX_CS_TYPE_TCP) { //TCP packet
                prSwRfb->aeCSUM[CSUM_TYPE_UDP] = CSUM_RES_NONE;
                if(u4TcpUdpIpCksStatus & RX_CS_STATUS_TCP) { //TCP packet csum failed
                    prSwRfb->aeCSUM[CSUM_TYPE_TCP] = CSUM_RES_FAILED;
                } else {
                    prSwRfb->aeCSUM[CSUM_TYPE_TCP] = CSUM_RES_SUCCESS;
                }
            }
            else if (u4TcpUdpIpCksStatus & RX_CS_TYPE_UDP) { //UDP packet
                prSwRfb->aeCSUM[CSUM_TYPE_TCP] = CSUM_RES_NONE;
                if(u4TcpUdpIpCksStatus & RX_CS_STATUS_UDP) { //UDP packet csum failed
                    prSwRfb->aeCSUM[CSUM_TYPE_UDP] = CSUM_RES_FAILED;
                } else {
                    prSwRfb->aeCSUM[CSUM_TYPE_UDP] = CSUM_RES_SUCCESS;
                }
            }
            else {
                prSwRfb->aeCSUM[CSUM_TYPE_UDP] = CSUM_RES_NONE;
                prSwRfb->aeCSUM[CSUM_TYPE_TCP] = CSUM_RES_NONE;
            }
        }
        else if (u4TcpUdpIpCksStatus & RX_CS_TYPE_IPv6) {//IPv6 packet
            prSwRfb->aeCSUM[CSUM_TYPE_IPV4] = CSUM_RES_NONE;
            prSwRfb->aeCSUM[CSUM_TYPE_IPV6] = CSUM_RES_SUCCESS;

            if (u4TcpUdpIpCksStatus & RX_CS_TYPE_TCP) { //TCP packet
                prSwRfb->aeCSUM[CSUM_TYPE_UDP] = CSUM_RES_NONE;
                if(u4TcpUdpIpCksStatus & RX_CS_STATUS_TCP) { //TCP packet csum failed
                    prSwRfb->aeCSUM[CSUM_TYPE_TCP] = CSUM_RES_FAILED;
                } else {
                    prSwRfb->aeCSUM[CSUM_TYPE_TCP] = CSUM_RES_SUCCESS;
                }
            }
            else if (u4TcpUdpIpCksStatus & RX_CS_TYPE_UDP) { //UDP packet
                prSwRfb->aeCSUM[CSUM_TYPE_TCP] = CSUM_RES_NONE;
                if(u4TcpUdpIpCksStatus & RX_CS_STATUS_UDP) { //UDP packet csum failed
                    prSwRfb->aeCSUM[CSUM_TYPE_UDP] = CSUM_RES_FAILED;
                } else {
                    prSwRfb->aeCSUM[CSUM_TYPE_UDP] = CSUM_RES_SUCCESS;
                }
            }
            else {
                prSwRfb->aeCSUM[CSUM_TYPE_UDP] = CSUM_RES_NONE;
                prSwRfb->aeCSUM[CSUM_TYPE_TCP] = CSUM_RES_NONE;
            }
        }
        else {
            prSwRfb->aeCSUM[CSUM_TYPE_IPV4] = CSUM_RES_NONE;
            prSwRfb->aeCSUM[CSUM_TYPE_IPV6] = CSUM_RES_NONE;
        }
    }

}
#endif /* CFG_TCP_IP_CHKSUM_OFFLOAD */


/*----------------------------------------------------------------------------*/
/*!
* @brief Process packet doesn't need to do buffer reordering
*
* @param prAdapter pointer to the Adapter handler
* @param prSWRfb the RFB to receive rx data
*
* @return (none)
*
*/
/*----------------------------------------------------------------------------*/
VOID
nicRxProcessPktWithoutReorder (
    IN P_ADAPTER_T prAdapter,
    IN P_SW_RFB_T  prSwRfb
    )
{
    P_RX_CTRL_T prRxCtrl;
    P_TX_CTRL_T prTxCtrl;
    BOOLEAN fgIsRetained = FALSE;
    UINT_32 u4CurrentRxBufferCount;
    P_STA_RECORD_T prStaRec = (P_STA_RECORD_T)NULL;

    DEBUGFUNC("nicRxProcessPktWithoutReorder");
    //DBGLOG(RX, TRACE, ("\n"));

    ASSERT(prAdapter);
    ASSERT(prSwRfb);

    prRxCtrl = &prAdapter->rRxCtrl;
    ASSERT(prRxCtrl);

    prTxCtrl = &prAdapter->rTxCtrl;
    ASSERT(prTxCtrl);

    u4CurrentRxBufferCount = prRxCtrl->rFreeSwRfbList.u4NumElem;
    /* QM USED = $A, AVAILABLE COUNT = $B, INDICATED TO OS = $C
     * TOTAL = $A + $B + $C
     *
     * Case #1 (Retain)
     * -------------------------------------------------------
     * $A + $B < THRESHOLD := $A + $B + $C < THRESHOLD + $C := $TOTAL - THRESHOLD < $C
     * => $C used too much, retain
     *
     * Case #2 (Non-Retain)
     * -------------------------------------------------------
     * $A + $B > THRESHOLD := $A + $B + $C > THRESHOLD + $C := $TOTAL - THRESHOLD > $C
     * => still availble for $C to use
     *
     */
    fgIsRetained = (((u4CurrentRxBufferCount +
                    qmGetRxReorderQueuedBufferCount(prAdapter) +
                    prTxCtrl->i4PendingFwdFrameCount) < CFG_RX_RETAINED_PKT_THRESHOLD) ?
                           TRUE : FALSE);

    //DBGLOG(RX, INFO, ("fgIsRetained = %d\n", fgIsRetained));

    if (kalProcessRxPacket(prAdapter->prGlueInfo,
                         prSwRfb->pvPacket,
                         prSwRfb->pvHeader,
                         (UINT_32)prSwRfb->u2PacketLen,
                         fgIsRetained,
                         prSwRfb->aeCSUM) != WLAN_STATUS_SUCCESS) {
        DBGLOG(RX, ERROR, ("kalProcessRxPacket return value != WLAN_STATUS_SUCCESS\n"));
        ASSERT(0);

        nicRxReturnRFB(prAdapter, prSwRfb);
        return;
    }
    else {
        prStaRec = cnmGetStaRecByIndex(prAdapter, prSwRfb->ucStaRecIdx);

        if (prStaRec) {
#if CFG_ENABLE_WIFI_DIRECT
            if (prStaRec->ucNetTypeIndex == NETWORK_TYPE_P2P_INDEX &&
                prAdapter->fgIsP2PRegistered == TRUE) {
                GLUE_SET_PKT_FLAG_P2P(prSwRfb->pvPacket);
            }
#endif
#if CFG_ENABLE_BT_OVER_WIFI
            if (prStaRec->ucNetTypeIndex == NETWORK_TYPE_BOW_INDEX) {
                GLUE_SET_PKT_FLAG_PAL(prSwRfb->pvPacket);
            }
#endif

			/* record the count to pass to os */
			STATS_RX_PASS2OS_INC(prStaRec, prSwRfb);
        }
        prRxCtrl->apvIndPacket[prRxCtrl->ucNumIndPacket] = prSwRfb->pvPacket;
        prRxCtrl->ucNumIndPacket++;
    }

    if (fgIsRetained) {
        prRxCtrl->apvRetainedPacket[prRxCtrl->ucNumRetainedPacket] = prSwRfb->pvPacket;
        prRxCtrl->ucNumRetainedPacket++;
            /* TODO : error handling of nicRxSetupRFB */
        nicRxSetupRFB(prAdapter, prSwRfb);
        nicRxReturnRFB(prAdapter, prSwRfb);
    }
    else{
        prSwRfb->pvPacket = NULL;
        nicRxReturnRFB(prAdapter, prSwRfb);
    }
}


/*----------------------------------------------------------------------------*/
/*!
* @brief Process forwarding data packet
*
* @param prAdapter pointer to the Adapter handler
* @param prSWRfb the RFB to receive rx data
*
* @return (none)
*
*/
/*----------------------------------------------------------------------------*/
VOID
nicRxProcessForwardPkt (
    IN P_ADAPTER_T prAdapter,
    IN P_SW_RFB_T  prSwRfb
    )
{
    P_MSDU_INFO_T prMsduInfo, prRetMsduInfoList;
    P_TX_CTRL_T prTxCtrl;
    P_RX_CTRL_T prRxCtrl;
    KAL_SPIN_LOCK_DECLARATION();

    DEBUGFUNC("nicRxProcessForwardPkt");

    ASSERT(prAdapter);
    ASSERT(prSwRfb);

    prTxCtrl = &prAdapter->rTxCtrl;
    prRxCtrl = &prAdapter->rRxCtrl;

    KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_TX_MSDU_INFO_LIST);
    QUEUE_REMOVE_HEAD(&prTxCtrl->rFreeMsduInfoList, prMsduInfo, P_MSDU_INFO_T);
    KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_TX_MSDU_INFO_LIST);

    if(prMsduInfo && kalProcessRxPacket(prAdapter->prGlueInfo,
                prSwRfb->pvPacket,
                prSwRfb->pvHeader,
                (UINT_32)prSwRfb->u2PacketLen,
                prRxCtrl->rFreeSwRfbList.u4NumElem < CFG_RX_RETAINED_PKT_THRESHOLD ? TRUE : FALSE,
                prSwRfb->aeCSUM) == WLAN_STATUS_SUCCESS) {

        prMsduInfo->eSrc = TX_PACKET_FORWARDING;
        // pack into MSDU_INFO_T
        nicTxFillMsduInfo(prAdapter, prMsduInfo, (P_NATIVE_PACKET)(prSwRfb->pvPacket));
        // Overwrite the ucNetworkType
        prMsduInfo->ucNetworkType = HIF_RX_HDR_GET_NETWORK_IDX(prSwRfb->prHifRxHdr);

        // release RX buffer (to rIndicatedRfbList)
        prSwRfb->pvPacket = NULL;
        nicRxReturnRFB(prAdapter, prSwRfb);

        // increase forward frame counter
        GLUE_INC_REF_CNT(prTxCtrl->i4PendingFwdFrameCount);

        // send into TX queue
        KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_QM_TX_QUEUE);
        prRetMsduInfoList = qmEnqueueTxPackets(prAdapter, prMsduInfo);
        KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_QM_TX_QUEUE);

        if(prRetMsduInfoList != NULL) { // TX queue refuses queuing the packet
            nicTxFreeMsduInfoPacket(prAdapter, prRetMsduInfoList);
            nicTxReturnMsduInfo(prAdapter, prRetMsduInfoList);
        }
        /* indicate service thread for sending */
        if(prTxCtrl->i4PendingFwdFrameCount > 0) {
            kalSetEvent(prAdapter->prGlueInfo);
        }
    }
    else { // no TX resource
        nicRxReturnRFB(prAdapter, prSwRfb);
    }

    return;
}


/*----------------------------------------------------------------------------*/
/*!
* @brief Process broadcast data packet for both host and forwarding
*
* @param prAdapter pointer to the Adapter handler
* @param prSWRfb the RFB to receive rx data
*
* @return (none)
*
*/
/*----------------------------------------------------------------------------*/
VOID
nicRxProcessGOBroadcastPkt (
    IN P_ADAPTER_T prAdapter,
    IN P_SW_RFB_T  prSwRfb
    )
{
    P_SW_RFB_T prSwRfbDuplicated;
    P_TX_CTRL_T prTxCtrl;
    P_RX_CTRL_T prRxCtrl;
    P_HIF_RX_HEADER_T prHifRxHdr;

    KAL_SPIN_LOCK_DECLARATION();

    DEBUGFUNC("nicRxProcessGOBroadcastPkt");

    ASSERT(prAdapter);
    ASSERT(prSwRfb);

    prTxCtrl = &prAdapter->rTxCtrl;
    prRxCtrl = &prAdapter->rRxCtrl;

    prHifRxHdr = prSwRfb->prHifRxHdr;
    ASSERT(prHifRxHdr);

    ASSERT(CFG_NUM_OF_QM_RX_PKT_NUM >= 16);

    if( prRxCtrl->rFreeSwRfbList.u4NumElem
                    >= (CFG_RX_MAX_PKT_NUM - (CFG_NUM_OF_QM_RX_PKT_NUM - 16 /* Reserved for others */) )  ) {

        /* 1. Duplicate SW_RFB_T */
        KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_RX_QUE);
        QUEUE_REMOVE_HEAD(&prRxCtrl->rFreeSwRfbList, prSwRfbDuplicated, P_SW_RFB_T);
        KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_RX_QUE);

        if(prSwRfbDuplicated){
            kalMemCopy(prSwRfbDuplicated->pucRecvBuff,
                    prSwRfb->pucRecvBuff,
                    ALIGN_4(prHifRxHdr->u2PacketLen + HIF_RX_HW_APPENDED_LEN));

            prSwRfbDuplicated->ucPacketType = HIF_RX_PKT_TYPE_DATA;
            prSwRfbDuplicated->ucStaRecIdx = (UINT_8)(prHifRxHdr->ucStaRecIdx);
            nicRxFillRFB(prAdapter, prSwRfbDuplicated);

            /* 2. Modify eDst */
            prSwRfbDuplicated->eDst = RX_PKT_DESTINATION_FORWARD;

            /* 4. Forward */
            nicRxProcessForwardPkt(prAdapter, prSwRfbDuplicated);
        }
    }
    else {
        DBGLOG(RX, WARN, ("Stop to forward BMC packet due to less free Sw Rfb %u\n",
			prRxCtrl->rFreeSwRfbList.u4NumElem));
    }

    /* 3. Indicate to host */
    prSwRfb->eDst = RX_PKT_DESTINATION_HOST;
    nicRxProcessPktWithoutReorder(prAdapter, prSwRfb);

    return;
}


/*----------------------------------------------------------------------------*/
/*!
* @brief Process HIF data packet
*
* @param prAdapter pointer to the Adapter handler
* @param prSWRfb the RFB to receive rx data
*
* @return (none)
*
*/
/*----------------------------------------------------------------------------*/
VOID
nicRxProcessDataPacket (
    IN P_ADAPTER_T    prAdapter,
    IN OUT P_SW_RFB_T prSwRfb
    )
{
    P_RX_CTRL_T prRxCtrl;
    P_SW_RFB_T prRetSwRfb, prNextSwRfb;
    P_HIF_RX_HEADER_T prHifRxHdr;
    P_STA_RECORD_T prStaRec;
	BOOLEAN		fIsDummy=FALSE;
	UINT_16 u2Etype = 0;
	BOOLEAN fgIsSkipClass3Chk = FALSE;

    DEBUGFUNC("nicRxProcessDataPacket");
    //DBGLOG(RX, TRACE, ("\n"));

    ASSERT(prAdapter);
    ASSERT(prSwRfb);

    prHifRxHdr = prSwRfb->prHifRxHdr;
    prRxCtrl = &prAdapter->rRxCtrl;

	fIsDummy=(prHifRxHdr->u2PacketLen >= 12)?FALSE:TRUE;
	
    nicRxFillRFB(prAdapter, prSwRfb);

#if 1 /* Check 1x Pkt */
    if (prSwRfb->u2PacketLen > 14) {
        PUINT_8 pc = (PUINT_8)prSwRfb->pvHeader;
//        UINT_16 u2Etype = 0;

        u2Etype = (pc[ETH_TYPE_LEN_OFFSET] << 8) | (pc[ETH_TYPE_LEN_OFFSET + 1]);

#if CFG_SUPPORT_WAPI
        if (u2Etype == ETH_P_1X || u2Etype == ETH_WPI_1X) {
            DBGLOG(RSN, INFO, ("R1X len=%d\n", prSwRfb->u2PacketLen));
        }
#else
        if (u2Etype == ETH_P_1X) {
            DBGLOG(RSN, INFO, ("R1X len=%d\n", prSwRfb->u2PacketLen));
        }
#endif
        else if (u2Etype == ETH_P_PRE_1X) {
            DBGLOG(RSN, INFO, ("Pre R1X len=%d\n", prSwRfb->u2PacketLen));
        }
    }
#endif

#if CFG_TCP_IP_CHKSUM_OFFLOAD || CFG_TCP_IP_CHKSUM_OFFLOAD_NDIS_60
    {
        UINT_32 u4TcpUdpIpCksStatus;

        u4TcpUdpIpCksStatus = *((PUINT_32)((ULONG)prHifRxHdr +
                (UINT_32)(ALIGN_4(prHifRxHdr->u2PacketLen))));
        nicRxFillChksumStatus(prAdapter, prSwRfb, u4TcpUdpIpCksStatus);

    }
#endif /* CFG_TCP_IP_CHKSUM_OFFLOAD */

    prStaRec = cnmGetStaRecByIndex(prAdapter, prHifRxHdr->ucStaRecIdx);

	if ((u2Etype == ETH_P_1X) || (u2Etype == ETH_P_PRE_1X)) 
	{
		if ((prStaRec != NULL) && 
				(prStaRec->eAuthAssocState == SAA_STATE_WAIT_ASSOC2)) 
		{ 
			/* skip to check class 3 error */ 
			fgIsSkipClass3Chk = TRUE; 
		} 
	} 
	
	if((fgIsSkipClass3Chk == TRUE) || 
			(secCheckClassError(prAdapter, prSwRfb, prStaRec) == TRUE && 
			 prAdapter->fgTestMode == FALSE)) { 
	
	//	  if(secCheckClassError(prAdapter, prSwRfb, prStaRec) == TRUE &&
	//		 prAdapter->fgTestMode == FALSE) {
#if CFG_HIF_RX_STARVATION_WARNING
        prRxCtrl->u4QueuedCnt++;
#endif

        if((prRetSwRfb = qmHandleRxPackets(prAdapter, prSwRfb)) != NULL) {
            do {
                // save next first
                prNextSwRfb = (P_SW_RFB_T)QUEUE_GET_NEXT_ENTRY((P_QUE_ENTRY_T)prRetSwRfb);
					if ( fIsDummy==TRUE ) {
						nicRxReturnRFB(prAdapter, prRetSwRfb);
			            RX_INC_CNT(prRxCtrl, RX_DROP_TOTAL_COUNT);
                                    DBGLOG(RX, WARN, ("Drop Dummy Packets"));

					} else {
                switch(prRetSwRfb->eDst) {
                case RX_PKT_DESTINATION_HOST:
                    nicRxProcessPktWithoutReorder(prAdapter, prRetSwRfb);
                    break;

                case RX_PKT_DESTINATION_FORWARD:
                    nicRxProcessForwardPkt(prAdapter, prRetSwRfb);
                    break;

                case RX_PKT_DESTINATION_HOST_WITH_FORWARD:
                    nicRxProcessGOBroadcastPkt(prAdapter, prRetSwRfb);
                    break;

                case RX_PKT_DESTINATION_NULL:
                    nicRxReturnRFB(prAdapter, prRetSwRfb);
                    RX_INC_CNT(prRxCtrl, RX_DST_NULL_DROP_COUNT);
                    RX_INC_CNT(prRxCtrl, RX_DROP_TOTAL_COUNT);
                    break;

                default:
                    break;
                }
                }
#if CFG_HIF_RX_STARVATION_WARNING
                prRxCtrl->u4DequeuedCnt++;
#endif
                prRetSwRfb = prNextSwRfb;
            } while(prRetSwRfb);
        }
    }
    else {
        nicRxReturnRFB(prAdapter, prSwRfb);
        RX_INC_CNT(prRxCtrl, RX_CLASS_ERR_DROP_COUNT);
        RX_INC_CNT(prRxCtrl, RX_DROP_TOTAL_COUNT);
    }
}


/*----------------------------------------------------------------------------*/
/*!
* @brief Process HIF event packet
*
* @param prAdapter pointer to the Adapter handler
* @param prSWRfb the RFB to receive rx data
*
* @return (none)
*
*/
/*----------------------------------------------------------------------------*/

UINT_8
nicRxProcessGSCNEvent (
    IN P_ADAPTER_T    prAdapter,
    IN OUT P_SW_RFB_T prSwRfb
    )
{

    P_CMD_INFO_T prCmdInfo;
    P_MSDU_INFO_T prMsduInfo;
    P_WIFI_EVENT_T prEvent;
    P_GLUE_INFO_T prGlueInfo;

    UINT_32 real_num = 0;


    DEBUGFUNC("nicRxProcessGSCNEvent");
    //DBGLOG(RX, TRACE, ("\n"));

    DBGLOG(SCN, INFO, ("nicRxProcessGSCNEvent  \n"));


    ASSERT(prAdapter);
    ASSERT(prSwRfb);

    prEvent = (P_WIFI_EVENT_T) prSwRfb->pucRecvBuff;
    prGlueInfo = prAdapter->prGlueInfo;


	struct sk_buff *skb;

	/* Alloc the SKB for vendor_event */


	/* Push the data to the skb */

    
    struct wiphy *wiphy =priv_to_wiphy(prGlueInfo); 

    wiphy = priv_to_wiphy(prGlueInfo);

    //prGlueInfo->
        
    DBGLOG(SCN, INFO, ("Event Handling  \n"));

    // Event Handling
    switch(prEvent->ucEID) {
        case EVENT_ID_GSCAN_SCAN_AVAILABLE:
        {

            DBGLOG(SCN, INFO, ("EVENT_ID_GSCAN_SCAN_AVAILABLE  \n"));

                
            P_EVENT_GSCAN_SCAN_AVAILABLE_T prEventGscnAvailable;
                        
            prEventGscnAvailable = (P_EVENT_GSCAN_SCAN_AVAILABLE_T)(prEvent->aucBuffer);
                        
            memcpy(prEventGscnAvailable,(P_EVENT_GSCAN_SCAN_AVAILABLE_T)(prEvent->aucBuffer), sizeof(EVENT_GSCAN_SCAN_AVAILABLE_T) );
            
            mtk_cfg80211_vendor_event_scan_results_avaliable(wiphy ,prEventGscnAvailable,prEventGscnAvailable->u2Num);
                            
            #if 0
            skb = cfg80211_vendor_cmd_alloc_reply_skb(prGlueInfo->prDevHandler , sizeof(EVENT_GSCAN_CAPABILITY_T));
            if (unlikely(!skb)) {
                WL_ERR(("skb alloc failed"));
                return -ENOMEM;
            }  
            #endif
                //nla_put_nohdr(skb, sizeof(EVENT_GSCAN_CAPABILITY_T), prEventGscnCapbiblity);
        }

        break;

        case EVENT_ID_GSCAN_RESULT:
        {
            DBGLOG(SCN, INFO, ("EVENT_ID_GSCAN_RESULT 2 \n"));
            
            P_EVENT_GSCAN_RESULT_T prEventBuffer;
            P_WIFI_GSCAN_RESULT_T prEventGscnResult;

            prEventBuffer = (P_EVENT_GSCAN_RESULT_T)(prEvent->aucBuffer);
            prEventGscnResult = prEventBuffer->rResult;
/*
    the following event struct should moved to kal and use the kal api to avoid future porting effort 

*/

            INT_32 i4Status = -EINVAL;
            struct sk_buff *skb;
            struct nlattr *attr;
            UINT_32 scan_id;
            UINT_8  scan_flag;
            
            P_PARAM_WIFI_GSCAN_RESULT prResults;

            scan_id = prEventBuffer->u2ScanId;
            scan_flag = prEventBuffer->u2ScanFlags;
            real_num = prEventBuffer->u2NumOfResults;

            printk("scan_id=%d, scan_flag =%d, real_num=%d\r\n", scan_id, scan_flag, real_num);

            skb = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, sizeof(PARAM_WIFI_GSCAN_RESULT) * real_num);
            if(!skb) {
                DBGLOG(RX, TRACE, ("%s allocate skb failed:%x\n", __FUNCTION__, i4Status));
                return -ENOMEM;
            }

            attr = nla_nest_start(skb, GSCAN_ATTRIBUTE_SCAN_RESULTS);
            {
                NLA_PUT_U32(skb, GSCAN_ATTRIBUTE_SCAN_ID, scan_id);
                NLA_PUT_U8(skb, GSCAN_ATTRIBUTE_SCAN_FLAGS, 1);
                NLA_PUT_U32(skb, GSCAN_ATTRIBUTE_NUM_OF_RESULTS, real_num);
                prResults = (P_PARAM_WIFI_GSCAN_RESULT)prEventGscnResult;
                if(prResults)
                    printk("ssid=%s, rssi=%d, channel=%d \r\n", prResults->ssid, prResults->rssi, prResults->channel);
                NLA_PUT(skb, GSCAN_ATTRIBUTE_SCAN_RESULTS, sizeof(PARAM_WIFI_GSCAN_RESULT) * real_num, prResults);
                printk("NLA_PUT scan results over \t");
            }
            nla_nest_end(skb, attr);
            if(1) //report_events=1
            NLA_PUT_U8(skb, GSCAN_ATTRIBUTE_SCAN_RESULTS_COMPLETE, 1);
           
            i4Status = cfg80211_vendor_cmd_reply(skb);    

            printk(" i4Status %d \n",i4Status );
                  
        }
        break;

        case EVENT_ID_GSCAN_CAPABILITY:
        {

            DBGLOG(SCN, INFO, ("EVENT_ID_GSCAN_CAPABILITY  \n"));
            
            P_EVENT_GSCAN_CAPABILITY_T prEventGscnCapbiblity;
            
            prEventGscnCapbiblity = (P_EVENT_GSCAN_CAPABILITY_T)(prEvent->aucBuffer);
            
            memcpy(prEventGscnCapbiblity,(P_EVENT_GSCAN_CAPABILITY_T)(prEvent->aucBuffer), sizeof(EVENT_GSCAN_CAPABILITY_T) );

            mtk_cfg80211_vendor_get_capabilities( wiphy,prGlueInfo->prDevHandler ,prEventGscnCapbiblity,sizeof(EVENT_GSCAN_CAPABILITY_T));
                
            #if 0
                skb = cfg80211_vendor_cmd_alloc_reply_skb(prGlueInfo->prDevHandler , sizeof(EVENT_GSCAN_CAPABILITY_T));
                if (unlikely(!skb)) {
                    WL_ERR(("skb alloc failed"));
		            return -ENOMEM;
	            }  
                #endif
        	    //nla_put_nohdr(skb, sizeof(EVENT_GSCAN_CAPABILITY_T), prEventGscnCapbiblity);
        }
        break;

            
        case EVENT_ID_GSCAN_SCAN_COMPLETE:   
        {
            DBGLOG(SCN, INFO, ("EVENT_ID_GSCAN_SCAN_COMPLETE  \n"));
            
            P_EVENT_GSCAN_SCAN_COMPLETE_T prEventGscnScnDone;
            
            prEventGscnScnDone = (P_EVENT_GSCAN_SCAN_COMPLETE_T)(prEvent->aucBuffer);
            
            memcpy(prEventGscnScnDone, (P_EVENT_GSCAN_SCAN_COMPLETE_T)(prEvent->aucBuffer), sizeof(EVENT_GSCAN_SCAN_COMPLETE_T) );

            mtk_cfg80211_vendor_event_complete_scan( wiphy ,prGlueInfo->prDevHandler,prEventGscnScnDone->ucScanState);
                
            #if 0
            skb = cfg80211_vendor_cmd_alloc_reply_skb(prGlueInfo->prDevHandler, sizeof(EVENT_GSCAN_SCAN_COMPLETE_T));
            if (unlikely(!skb)) {
                WL_ERR(("skb alloc failed"));
                return -ENOMEM;
            }      
            #endif
        	    //nla_put_nohdr(skb, sizeof(EVENT_GSCAN_SCAN_COMPLETE_T), prEventGscnScnDone); 
        }
        break;

            
        case EVENT_ID_GSCAN_FULL_RESULT:  
        {

            DBGLOG(SCN, INFO, ("EVENT_ID_GSCAN_FULL_RESULT  \n"));
            
            P_WIFI_GSCAN_RESULT_T prEventGscnFullResultl;
            P_PARAM_WIFI_GSCAN_RESULT prParamGscnFullResultl;
                    
            prEventGscnFullResultl = (P_EVENT_GSCAN_FULL_RESULT_T)((prEvent->aucBuffer)+ sizeof(EVENT_GSCAN_FULL_RESULT_T));      

            prEventGscnFullResultl = kalMemAlloc( sizeof(WIFI_GSCAN_RESULT_T), VIR_MEM_TYPE );
            memcpy(prEventGscnFullResultl, (P_WIFI_GSCAN_RESULT_T)(prEvent->aucBuffer), sizeof(WIFI_GSCAN_RESULT_T) );
                
            prParamGscnFullResultl = kalMemAlloc( sizeof(PARAM_WIFI_GSCAN_RESULT), VIR_MEM_TYPE );
            kalMemZero(prParamGscnFullResultl, sizeof(PARAM_WIFI_GSCAN_RESULT));  
            memcpy(prParamGscnFullResultl, prEventGscnFullResultl, sizeof(WIFI_GSCAN_RESULT_T) );

            mtk_cfg80211_vendor_event_full_scan_results( wiphy ,
                prGlueInfo->prDevHandler,
                prParamGscnFullResultl,
                sizeof(PARAM_WIFI_GSCAN_RESULT));
                
            #if 0
            skb = cfg80211_vendor_cmd_alloc_reply_skb(prGlueInfo->prDevHandler, sizeof(EVENT_GSCAN_FULL_RESULT_T));
            if (unlikely(!skb)) {
                WL_ERR(("skb alloc failed"));
                return -ENOMEM;
            }   
            #endif
        	    //nla_put_nohdr(skb, sizeof(EVENT_GSCAN_FULL_RESULT_T), prEventGscnFullResultl);    
        }
        break;
            
        case EVENT_ID_GSCAN_SIGNIFICANT_CHANGE:
        {
            P_EVENT_GSCAN_SIGNIFICANT_CHANGE_T prEventGscnSignigicatCange;
            prEventGscnSignigicatCange = (P_EVENT_GSCAN_SIGNIFICANT_CHANGE_T)(prEvent->aucBuffer);
            
            memcpy(prEventGscnSignigicatCange,(P_EVENT_GSCAN_SIGNIFICANT_CHANGE_T)(prEvent->aucBuffer), sizeof(EVENT_GSCAN_SIGNIFICANT_CHANGE_T) );
                
            #if 0
            skb = cfg80211_vendor_cmd_alloc_reply_skb(prGlueInfo->prDevHandler, sizeof(EVENT_GSCAN_SIGNIFICANT_CHANGE_T));
            if (unlikely(!skb)) {
                WL_ERR(("skb alloc failed"));
                return -ENOMEM;
            }             
            #endif
        	    //nla_put_nohdr(skb, sizeof(EVENT_GSCAN_SIGNIFICANT_CHANGE_T), prEventGscnSignigicatCange);    
        }
        break;
            
        case EVENT_ID_GSCAN_GEOFENCE_FOUND: 
        {
            P_EVENT_GSCAN_SIGNIFICANT_CHANGE_T prEventGscnGeofenceFound;  
            
            prEventGscnGeofenceFound = (P_EVENT_GSCAN_SIGNIFICANT_CHANGE_T)(prEvent->aucBuffer);
            
            memcpy(prEventGscnGeofenceFound, (P_EVENT_GSCAN_SIGNIFICANT_CHANGE_T)(prEvent->aucBuffer), sizeof(EVENT_GSCAN_SIGNIFICANT_CHANGE_T) );
            #if 0
            skb = cfg80211_vendor_cmd_alloc_reply_skb(prGlueInfo->prDevHandler, sizeof(EVENT_GSCAN_SIGNIFICANT_CHANGE_T));
            if (unlikely(!skb)) {
                WL_ERR(("skb alloc failed"));
                return -ENOMEM;
            }   
            #endif
        	    //nla_put_nohdr(skb, sizeof(EVENT_GSCAN_SIGNIFICANT_CHANGE_T), prEventGscnGeofenceFound);
        }
        break;
     
        default:
            printk("not GSCN event ????\n"); 
        break;
    }


    DBGLOG(SCN, INFO, ("Done with GSCN event handling  \n"));

    
    return real_num; //cfg80211_vendor_cmd_reply(skb);

    nla_put_failure: 

    printk("nla_put_failure \n");

    return 0; //cfg80211_vendor_cmd_reply(skb);


}


/*----------------------------------------------------------------------------*/
/*!
* @brief Process HIF event packet
*
* @param prAdapter pointer to the Adapter handler
* @param prSWRfb the RFB to receive rx data
*
* @return (none)
*
*/
/*----------------------------------------------------------------------------*/
VOID
nicRxProcessEventPacket (
    IN P_ADAPTER_T    prAdapter,
    IN OUT P_SW_RFB_T prSwRfb
    )
{
    P_CMD_INFO_T prCmdInfo;
    P_MSDU_INFO_T prMsduInfo;
    P_WIFI_EVENT_T prEvent;
    P_GLUE_INFO_T prGlueInfo;

    BOOLEAN fgKeepprSwRfb = FALSE;


    DEBUGFUNC("nicRxProcessEventPacket");
    //DBGLOG(RX, TRACE, ("\n"));

    ASSERT(prAdapter);
    ASSERT(prSwRfb);

    prEvent = (P_WIFI_EVENT_T) prSwRfb->pucRecvBuff;
    prGlueInfo = prAdapter->prGlueInfo;

	DBGLOG(RX, TRACE, ("prEvent->ucEID = %d\n", prEvent->ucEID));
    // Event Handling
    switch(prEvent->ucEID) {
		case EVENT_ID_WARNING_TO_DRIVER:
		{
		P_EVENT_LOG_TO_DRIVER_T prEventLog;
		UINT_32 UpTimeSec, UpTimeMicroSec;

		prEventLog = (P_EVENT_LOG_TO_DRIVER_T)(prEvent->aucBuffer);
		UpTimeSec = prEventLog->WifiUpTime/1000000;
		UpTimeMicroSec = prEventLog->WifiUpTime%1000000;
		LOG_FUNC("[%d.%d] FW Warning!! %s: %d, %s\n", UpTimeSec, UpTimeMicroSec, prEventLog->fileName, prEventLog->lineNo, prEventLog->log);

		break;
		}	
    case EVENT_ID_CMD_RESULT:
        prCmdInfo = nicGetPendingCmdInfo(prAdapter, prEvent->ucSeqNum);

        if(prCmdInfo != NULL) {
            P_EVENT_CMD_RESULT prCmdResult;
            prCmdResult = (P_EVENT_CMD_RESULT) ((PUINT_8)prEvent + EVENT_HDR_SIZE);

            /* CMD_RESULT should be only in response to Set commands */
            ASSERT(prCmdInfo->fgSetQuery == FALSE || prCmdInfo->fgNeedResp == TRUE);

            if(prCmdResult->ucStatus == 0) { // success
                if(prCmdInfo->pfCmdDoneHandler) {
                    prCmdInfo->pfCmdDoneHandler(prAdapter, prCmdInfo, prEvent->aucBuffer);
                }
                else if(prCmdInfo->fgIsOid == TRUE) {
                    kalOidComplete(prAdapter->prGlueInfo, prCmdInfo->fgSetQuery, 0, WLAN_STATUS_SUCCESS);
                }
            }
            else if(prCmdResult->ucStatus == 1) { // reject
                if(prCmdInfo->fgIsOid == TRUE)
                    kalOidComplete(prAdapter->prGlueInfo, prCmdInfo->fgSetQuery, 0, WLAN_STATUS_FAILURE);
            }
            else if(prCmdResult->ucStatus == 2) { // unknown CMD
                if(prCmdInfo->fgIsOid == TRUE)
                    kalOidComplete(prAdapter->prGlueInfo, prCmdInfo->fgSetQuery, 0, WLAN_STATUS_NOT_SUPPORTED);
            }

            // return prCmdInfo
            cmdBufFreeCmdInfo(prAdapter, prCmdInfo);
        }

        break;

#if 0
    case EVENT_ID_CONNECTION_STATUS:
        /* OBSELETE */
        {
            P_EVENT_CONNECTION_STATUS prConnectionStatus;
            prConnectionStatus = (P_EVENT_CONNECTION_STATUS) (prEvent->aucBuffer);

            DbgPrint("RX EVENT: EVENT_ID_CONNECTION_STATUS = %d\n", prConnectionStatus->ucMediaStatus);
            if (prConnectionStatus->ucMediaStatus == PARAM_MEDIA_STATE_DISCONNECTED) { // disconnected
                if(kalGetMediaStateIndicated(prGlueInfo) != PARAM_MEDIA_STATE_DISCONNECTED) {

                    kalIndicateStatusAndComplete(prGlueInfo,
                            WLAN_STATUS_MEDIA_DISCONNECT,
                            NULL,
                            0);

                    prAdapter->rWlanInfo.u4SysTime = kalGetTimeTick();
                }
            }
            else if(prConnectionStatus->ucMediaStatus == PARAM_MEDIA_STATE_CONNECTED) { // connected
                prAdapter->rWlanInfo.u4SysTime = kalGetTimeTick();

                // fill information for association result
                prAdapter->rWlanInfo.rCurrBssId.rSsid.u4SsidLen
                    = prConnectionStatus->ucSsidLen;
                kalMemCopy(prAdapter->rWlanInfo.rCurrBssId.rSsid.aucSsid,
                        prConnectionStatus->aucSsid,
                        prConnectionStatus->ucSsidLen);

                kalMemCopy(prAdapter->rWlanInfo.rCurrBssId.arMacAddress,
                        prConnectionStatus->aucBssid,
                        MAC_ADDR_LEN);

                prAdapter->rWlanInfo.rCurrBssId.u4Privacy
                    = prConnectionStatus->ucEncryptStatus; // @FIXME
                prAdapter->rWlanInfo.rCurrBssId.rRssi
                    = 0; //@FIXME
                prAdapter->rWlanInfo.rCurrBssId.eNetworkTypeInUse
                    = PARAM_NETWORK_TYPE_AUTOMODE; //@FIXME
                prAdapter->rWlanInfo.rCurrBssId.rConfiguration.u4BeaconPeriod
                    = prConnectionStatus->u2BeaconPeriod;
                prAdapter->rWlanInfo.rCurrBssId.rConfiguration.u4ATIMWindow
                    = prConnectionStatus->u2ATIMWindow;
                prAdapter->rWlanInfo.rCurrBssId.rConfiguration.u4DSConfig
                    = prConnectionStatus->u4FreqInKHz;
                prAdapter->rWlanInfo.ucNetworkType
                    = prConnectionStatus->ucNetworkType;

                switch(prConnectionStatus->ucInfraMode) {
                case 0:
                    prAdapter->rWlanInfo.rCurrBssId.eOpMode = NET_TYPE_IBSS;
                    break;
                case 1:
                    prAdapter->rWlanInfo.rCurrBssId.eOpMode = NET_TYPE_INFRA;
                    break;
                case 2:
                default:
                    prAdapter->rWlanInfo.rCurrBssId.eOpMode = NET_TYPE_AUTO_SWITCH;
                    break;
                }
                // always indicate to OS according to MSDN (re-association/roaming)
                kalIndicateStatusAndComplete(prGlueInfo,
                        WLAN_STATUS_MEDIA_CONNECT,
                        NULL,
                        0);
            }
        }
        break;

    case EVENT_ID_SCAN_RESULT:
        /* OBSELETE */
        break;
#endif

    case EVENT_ID_RX_ADDBA:
        /* The FW indicates that an RX BA agreement will be established */
        qmHandleEventRxAddBa(prAdapter, prEvent);
        break;

    case EVENT_ID_RX_DELBA:
        /* The FW indicates that an RX BA agreement has been deleted */
        qmHandleEventRxDelBa(prAdapter, prEvent);
        break;

    case EVENT_ID_LINK_QUALITY:
#if CFG_ENABLE_WIFI_DIRECT && CFG_SUPPORT_P2P_RSSI_QUERY
        if (prEvent->u2PacketLen == EVENT_HDR_SIZE + sizeof(EVENT_LINK_QUALITY_EX)) {
            P_EVENT_LINK_QUALITY_EX prLqEx = (P_EVENT_LINK_QUALITY_EX)(prEvent->aucBuffer);

            if (prLqEx->ucIsLQ0Rdy) {
                nicUpdateLinkQuality(prAdapter, NETWORK_TYPE_AIS_INDEX, (P_EVENT_LINK_QUALITY)prLqEx);
            }
            if (prLqEx->ucIsLQ1Rdy) {
                nicUpdateLinkQuality(prAdapter, NETWORK_TYPE_P2P_INDEX, (P_EVENT_LINK_QUALITY)prLqEx);
            }
        }
        else {
            /* For old FW, P2P may invoke link quality query, and make driver flag becone TRUE. */
            DBGLOG(P2P, WARN, ("Old FW version, not support P2P RSSI query.\n"));

            /* Must not use NETWORK_TYPE_P2P_INDEX, cause the structure is mismatch. */
        nicUpdateLinkQuality(prAdapter, NETWORK_TYPE_AIS_INDEX, (P_EVENT_LINK_QUALITY)(prEvent->aucBuffer));
        }
#else
        nicUpdateLinkQuality(prAdapter, NETWORK_TYPE_AIS_INDEX, (P_EVENT_LINK_QUALITY)(prEvent->aucBuffer));
#endif

        /* command response handling */
        prCmdInfo = nicGetPendingCmdInfo(prAdapter, prEvent->ucSeqNum);

        if(prCmdInfo != NULL) {
            if (prCmdInfo->pfCmdDoneHandler) {
                prCmdInfo->pfCmdDoneHandler(prAdapter, prCmdInfo, prEvent->aucBuffer);
            }
            else if(prCmdInfo->fgIsOid) {
                kalOidComplete(prAdapter->prGlueInfo, prCmdInfo->fgSetQuery, 0, WLAN_STATUS_SUCCESS);
            }

            // return prCmdInfo
            cmdBufFreeCmdInfo(prAdapter, prCmdInfo);
        }

        #ifndef LINUX
        if(prAdapter->rWlanInfo.eRssiTriggerType == ENUM_RSSI_TRIGGER_GREATER &&
                prAdapter->rWlanInfo.rRssiTriggerValue >= (PARAM_RSSI)(prAdapter->rLinkQuality.cRssi)) {
            prAdapter->rWlanInfo.eRssiTriggerType = ENUM_RSSI_TRIGGER_TRIGGERED;

            kalIndicateStatusAndComplete(prGlueInfo,
                    WLAN_STATUS_MEDIA_SPECIFIC_INDICATION,
                    (PVOID) &(prAdapter->rWlanInfo.rRssiTriggerValue), sizeof(PARAM_RSSI));
        }
        else if(prAdapter->rWlanInfo.eRssiTriggerType == ENUM_RSSI_TRIGGER_LESS &&
                prAdapter->rWlanInfo.rRssiTriggerValue <= (PARAM_RSSI)(prAdapter->rLinkQuality.cRssi)) {
            prAdapter->rWlanInfo.eRssiTriggerType = ENUM_RSSI_TRIGGER_TRIGGERED;

            kalIndicateStatusAndComplete(prGlueInfo,
                    WLAN_STATUS_MEDIA_SPECIFIC_INDICATION,
                    (PVOID) &(prAdapter->rWlanInfo.rRssiTriggerValue), sizeof(PARAM_RSSI));
        }
        #endif

        break;

    case EVENT_ID_MIC_ERR_INFO:
        {
            P_EVENT_MIC_ERR_INFO prMicError;
            //P_PARAM_AUTH_EVENT_T prAuthEvent;
            P_STA_RECORD_T prStaRec;

            DBGLOG(RSN, EVENT, ("EVENT_ID_MIC_ERR_INFO\n"));

            prMicError = (P_EVENT_MIC_ERR_INFO)(prEvent->aucBuffer);
            prStaRec = cnmGetStaRecByAddress(prAdapter,
                            (UINT_8) NETWORK_TYPE_AIS_INDEX,
                            prAdapter->rWlanInfo.rCurrBssId.arMacAddress);
            ASSERT(prStaRec);

            if (prStaRec) {
                rsnTkipHandleMICFailure(prAdapter, prStaRec, (BOOLEAN)prMicError->u4Flags);
            }
            else {
                DBGLOG(RSN, INFO, ("No STA rec!!\n"));
            }
#if 0
            prAuthEvent = (P_PARAM_AUTH_EVENT_T)prAdapter->aucIndicationEventBuffer;

            /* Status type: Authentication Event */
            prAuthEvent->rStatus.eStatusType = ENUM_STATUS_TYPE_AUTHENTICATION;

            /* Authentication request */
            prAuthEvent->arRequest[0].u4Length = sizeof(PARAM_AUTH_REQUEST_T);
            kalMemCopy((PVOID)prAuthEvent->arRequest[0].arBssid,
                (PVOID)prAdapter->rWlanInfo.rCurrBssId.arMacAddress, /* whsu:Todo? */
                PARAM_MAC_ADDR_LEN);

            if (prMicError->u4Flags != 0) {
                prAuthEvent->arRequest[0].u4Flags = PARAM_AUTH_REQUEST_GROUP_ERROR;
            }
            else {
                prAuthEvent->arRequest[0].u4Flags = PARAM_AUTH_REQUEST_PAIRWISE_ERROR;
            }

            kalIndicateStatusAndComplete(prAdapter->prGlueInfo,
                WLAN_STATUS_MEDIA_SPECIFIC_INDICATION,
                (PVOID)prAuthEvent,
                sizeof(PARAM_STATUS_INDICATION_T) + sizeof(PARAM_AUTH_REQUEST_T));
#endif
        }
        break;

    case EVENT_ID_ASSOC_INFO:
        {
            P_EVENT_ASSOC_INFO prAssocInfo;
            prAssocInfo = (P_EVENT_ASSOC_INFO)(prEvent->aucBuffer);

            kalHandleAssocInfo(prAdapter->prGlueInfo, prAssocInfo);
        }
        break;

    case EVENT_ID_802_11_PMKID:
        {
            P_PARAM_AUTH_EVENT_T           prAuthEvent;
            PUINT_8                        cp;
            UINT_32                        u4LenOfUsedBuffer;

            prAuthEvent = (P_PARAM_AUTH_EVENT_T)prAdapter->aucIndicationEventBuffer;

            prAuthEvent->rStatus.eStatusType = ENUM_STATUS_TYPE_CANDIDATE_LIST;

            u4LenOfUsedBuffer = (UINT_32)(prEvent->u2PacketLen - 8);

            prAuthEvent->arRequest[0].u4Length = u4LenOfUsedBuffer;

            cp = (PUINT_8)&prAuthEvent->arRequest[0];

            /* Status type: PMKID Candidatelist Event */
            kalMemCopy(cp, (P_EVENT_PMKID_CANDIDATE_LIST_T)(prEvent->aucBuffer), prEvent->u2PacketLen - 8);

            kalIndicateStatusAndComplete(prAdapter->prGlueInfo,
                WLAN_STATUS_MEDIA_SPECIFIC_INDICATION,
                (PVOID)prAuthEvent,
                sizeof(PARAM_STATUS_INDICATION_T) + u4LenOfUsedBuffer);
        }
        break;

#if 0
    case EVENT_ID_ACTIVATE_STA_REC_T:
        {
            P_EVENT_ACTIVATE_STA_REC_T prActivateStaRec;
            prActivateStaRec = (P_EVENT_ACTIVATE_STA_REC_T)(prEvent->aucBuffer);

            DbgPrint("RX EVENT: EVENT_ID_ACTIVATE_STA_REC_T Index:%d, MAC:["MACSTR"]\n",
                prActivateStaRec->ucStaRecIdx,
                MAC2STR(prActivateStaRec->aucMacAddr));

            qmActivateStaRec(prAdapter,
                             (UINT_32)prActivateStaRec->ucStaRecIdx,
                             ((prActivateStaRec->fgIsQoS) ? TRUE: FALSE),
                             prActivateStaRec->ucNetworkTypeIndex,
                             ((prActivateStaRec->fgIsAP) ? TRUE: FALSE),
                             prActivateStaRec->aucMacAddr);

        }
        break;

    case EVENT_ID_DEACTIVATE_STA_REC_T:
        {
            P_EVENT_DEACTIVATE_STA_REC_T prDeactivateStaRec;
            prDeactivateStaRec = (P_EVENT_DEACTIVATE_STA_REC_T)(prEvent->aucBuffer);

            DbgPrint("RX EVENT: EVENT_ID_DEACTIVATE_STA_REC_T Index:%d, MAC:["MACSTR"]\n",
                prDeactivateStaRec->ucStaRecIdx);

            qmDeactivateStaRec(prAdapter,
                               prDeactivateStaRec->ucStaRecIdx);
        }
        break;
#endif

    case EVENT_ID_SCAN_DONE:
        scnEventScanDone(prAdapter, (P_EVENT_SCAN_DONE)(prEvent->aucBuffer));
        break;

	case EVENT_ID_TX_DONE_STATUS:
		STATS_TX_PKT_DONE_INFO_DISPLAY(prAdapter, prEvent->aucBuffer);
		break;

    case EVENT_ID_TX_DONE:
        {
            P_EVENT_TX_DONE_T prTxDone;
            prTxDone = (P_EVENT_TX_DONE_T)(prEvent->aucBuffer);

            DBGLOG(RX, TRACE,("EVENT_ID_TX_DONE PacketSeq:%u ucStatus: %u SN: %u\n",
                prTxDone->ucPacketSeq, prTxDone->ucStatus, prTxDone->u2SequenceNumber));

            /* call related TX Done Handler */
            prMsduInfo = nicGetPendingTxMsduInfo(prAdapter, prTxDone->ucPacketSeq);

#if CFG_SUPPORT_802_11V_TIMING_MEASUREMENT
            DBGLOG(RX, TRACE, ("EVENT_ID_TX_DONE u4TimeStamp = %x u2AirDelay = %x\n", 
                prTxDone->au4Reserved1, prTxDone->au4Reserved2));
            
            wnmReportTimingMeas(prAdapter, prMsduInfo->ucStaRecIndex, 
                                prTxDone->au4Reserved1, prTxDone->au4Reserved1 + prTxDone->au4Reserved2);
#endif

            if(prMsduInfo) {
                prMsduInfo->pfTxDoneHandler(prAdapter, prMsduInfo, (ENUM_TX_RESULT_CODE_T)(prTxDone->ucStatus));

                cnmMgtPktFree(prAdapter, prMsduInfo);
            }
        }
        break;

    case EVENT_ID_SLEEPY_NOTIFY:
        {
            P_EVENT_SLEEPY_NOTIFY prEventSleepyNotify;
            prEventSleepyNotify = (P_EVENT_SLEEPY_NOTIFY)(prEvent->aucBuffer);

            //DBGLOG(RX, INFO, ("ucSleepyState = %d\n", prEventSleepyNotify->ucSleepyState));

            prAdapter->fgWiFiInSleepyState = (BOOLEAN)(prEventSleepyNotify->ucSleepyState);
        }
        break;
    case EVENT_ID_BT_OVER_WIFI:
#if CFG_ENABLE_BT_OVER_WIFI
        {
            UINT_8 aucTmp[sizeof(AMPC_EVENT) + sizeof(BOW_LINK_DISCONNECTED)];
            P_EVENT_BT_OVER_WIFI prEventBtOverWifi;
            P_AMPC_EVENT prBowEvent;
            P_BOW_LINK_CONNECTED prBowLinkConnected;
            P_BOW_LINK_DISCONNECTED prBowLinkDisconnected;

            prEventBtOverWifi = (P_EVENT_BT_OVER_WIFI)(prEvent->aucBuffer);

            // construct event header
            prBowEvent = (P_AMPC_EVENT)aucTmp;

            if(prEventBtOverWifi->ucLinkStatus == 0) {
                // Connection
                prBowEvent->rHeader.ucEventId = BOW_EVENT_ID_LINK_CONNECTED;
                prBowEvent->rHeader.ucSeqNumber = 0;
                prBowEvent->rHeader.u2PayloadLength = sizeof(BOW_LINK_CONNECTED);

                // fill event body
                prBowLinkConnected = (P_BOW_LINK_CONNECTED)(prBowEvent->aucPayload);
                prBowLinkConnected->rChannel.ucChannelNum = prEventBtOverWifi->ucSelectedChannel;
                kalMemZero(prBowLinkConnected->aucPeerAddress, MAC_ADDR_LEN); //@FIXME

                kalIndicateBOWEvent(prAdapter->prGlueInfo, prBowEvent);
            }
            else {
                // Disconnection
                prBowEvent->rHeader.ucEventId = BOW_EVENT_ID_LINK_DISCONNECTED;
                prBowEvent->rHeader.ucSeqNumber = 0;
                prBowEvent->rHeader.u2PayloadLength = sizeof(BOW_LINK_DISCONNECTED);

                // fill event body
                prBowLinkDisconnected = (P_BOW_LINK_DISCONNECTED)(prBowEvent->aucPayload);
                prBowLinkDisconnected->ucReason = 0; //@FIXME
                kalMemZero(prBowLinkDisconnected->aucPeerAddress, MAC_ADDR_LEN); //@FIXME

                kalIndicateBOWEvent(prAdapter->prGlueInfo, prBowEvent);
            }
        }
        break;
#endif
    case EVENT_ID_STATISTICS:
        /* buffer statistics for further query */
        prAdapter->fgIsStatValid = TRUE;
        prAdapter->rStatUpdateTime = kalGetTimeTick();
        kalMemCopy(&prAdapter->rStatStruct, prEvent->aucBuffer, sizeof(EVENT_STATISTICS));

        /* command response handling */
        prCmdInfo = nicGetPendingCmdInfo(prAdapter, prEvent->ucSeqNum);

        if(prCmdInfo != NULL) {
            if (prCmdInfo->pfCmdDoneHandler) {
                prCmdInfo->pfCmdDoneHandler(prAdapter, prCmdInfo, prEvent->aucBuffer);
            }
            else if(prCmdInfo->fgIsOid) {
                kalOidComplete(prAdapter->prGlueInfo, prCmdInfo->fgSetQuery, 0, WLAN_STATUS_SUCCESS);
            }

            // return prCmdInfo
            cmdBufFreeCmdInfo(prAdapter, prCmdInfo);
        }

        break;

    case EVENT_ID_CH_PRIVILEGE:
        cnmChMngrHandleChEvent(prAdapter, prEvent);
        break;

    case EVENT_ID_BSS_ABSENCE_PRESENCE:
        qmHandleEventBssAbsencePresence(prAdapter, prEvent);
        break;

    case EVENT_ID_STA_CHANGE_PS_MODE:
        qmHandleEventStaChangePsMode(prAdapter, prEvent);
        break;
#if CFG_ENABLE_WIFI_DIRECT
    case EVENT_ID_STA_UPDATE_FREE_QUOTA:
        qmHandleEventStaUpdateFreeQuota(prAdapter, prEvent);
        break;
#endif
    case EVENT_ID_BSS_BEACON_TIMEOUT:
        DBGLOG(RX, INFO,("EVENT_ID_BSS_BEACON_TIMEOUT\n"));

        if (prAdapter->fgDisBcnLostDetection == FALSE) {
            P_EVENT_BSS_BEACON_TIMEOUT_T prEventBssBeaconTimeout;
            prEventBssBeaconTimeout = (P_EVENT_BSS_BEACON_TIMEOUT_T)(prEvent->aucBuffer);

			DBGLOG(RX, INFO,("Reason = %u\n", prEventBssBeaconTimeout->ucReason));

            if(prEventBssBeaconTimeout->ucNetTypeIndex == NETWORK_TYPE_AIS_INDEX) {
                /* Request stats report before beacon timeout */
                P_BSS_INFO_T prBssInfo;
                P_STA_RECORD_T prStaRec;                                   
                prBssInfo = &(prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_AIS_INDEX]);
                if (prBssInfo)
                {
                    prStaRec = cnmGetStaRecByAddress(prAdapter,NETWORK_TYPE_AIS_INDEX, prBssInfo->aucBSSID);
                    if (prStaRec) {
                        STATS_ENV_REPORT_DETECT(prAdapter, prStaRec->ucIndex);                                          
                    }
                }                
                aisBssBeaconTimeout(prAdapter);
            }
#if CFG_ENABLE_WIFI_DIRECT
            else if((prAdapter->fgIsP2PRegistered) &&
                (prEventBssBeaconTimeout->ucNetTypeIndex == NETWORK_TYPE_P2P_INDEX)) {

                p2pFsmRunEventBeaconTimeout(prAdapter);
            }
#endif
#if CFG_ENABLE_BT_OVER_WIFI
            else if(prEventBssBeaconTimeout->ucNetTypeIndex == NETWORK_TYPE_BOW_INDEX) {
            }
#endif
            else {
                DBGLOG(RX, ERROR, ("EVENT_ID_BSS_BEACON_TIMEOUT: (ucNetTypeIdx = %d)\n",
                            prEventBssBeaconTimeout->ucNetTypeIndex));
            }
        }

        break;
    case EVENT_ID_UPDATE_NOA_PARAMS:
#if CFG_ENABLE_WIFI_DIRECT
        if(prAdapter->fgIsP2PRegistered){
            P_EVENT_UPDATE_NOA_PARAMS_T prEventUpdateNoaParam;
            prEventUpdateNoaParam = (P_EVENT_UPDATE_NOA_PARAMS_T)(prEvent->aucBuffer);

            if (prEventUpdateNoaParam->ucNetTypeIndex == NETWORK_TYPE_P2P_INDEX) {
                p2pProcessEvent_UpdateNOAParam(prAdapter,
                                                prEventUpdateNoaParam->ucNetTypeIndex,
                                                prEventUpdateNoaParam);
            } else {
                ASSERT(0);
            }
        }
#else
        ASSERT(0);
#endif
        break;

    case EVENT_ID_STA_AGING_TIMEOUT:
#if CFG_ENABLE_WIFI_DIRECT
        {
            if (prAdapter->fgDisStaAgingTimeoutDetection == FALSE) {
                P_EVENT_STA_AGING_TIMEOUT_T prEventStaAgingTimeout;
                P_STA_RECORD_T prStaRec;
                P_BSS_INFO_T prBssInfo = (P_BSS_INFO_T)NULL;

                prEventStaAgingTimeout = (P_EVENT_STA_AGING_TIMEOUT_T)(prEvent->aucBuffer);
                prStaRec = cnmGetStaRecByIndex(prAdapter, prEventStaAgingTimeout->ucStaRecIdx);
                if (prStaRec == NULL) {
                    break;
                }

                DBGLOG(RX, INFO,("EVENT_ID_STA_AGING_TIMEOUT %u " MACSTR "\n",
                                prEventStaAgingTimeout->ucStaRecIdx, MAC2STR(prStaRec->aucMacAddr)));

                prBssInfo = &(prAdapter->rWifiVar.arBssInfo[prStaRec->ucNetTypeIndex]);

                bssRemoveStaRecFromClientList(prAdapter, prBssInfo, prStaRec);
            
                /* Call False Auth */
                if (prAdapter->fgIsP2PRegistered) {
                    p2pFuncDisconnect(prAdapter, prStaRec, TRUE, REASON_CODE_DISASSOC_INACTIVITY);
                }

            
            } /* gDisStaAgingTimeoutDetection */

        }
#endif
        break;

    case EVENT_ID_AP_OBSS_STATUS:
#if CFG_ENABLE_WIFI_DIRECT
        if(prAdapter->fgIsP2PRegistered){
            rlmHandleObssStatusEventPkt(prAdapter, (P_EVENT_AP_OBSS_STATUS_T) prEvent->aucBuffer);
        }
#endif
        break;

    case EVENT_ID_ROAMING_STATUS:
#if CFG_SUPPORT_ROAMING
        {
            P_ROAMING_PARAM_T prParam;

            prParam = (P_ROAMING_PARAM_T)(prEvent->aucBuffer);
            roamingFsmProcessEvent(prAdapter, prParam);
        }
#endif /* CFG_SUPPORT_ROAMING */
        break;
    case EVENT_ID_SEND_DEAUTH:
		{
			P_WLAN_MAC_HEADER_T prWlanMacHeader;
			P_STA_RECORD_T prStaRec;

			prWlanMacHeader = (P_WLAN_MAC_HEADER_T)&prEvent->aucBuffer[0];
			DBGLOG(RSN, INFO, ("nicRx: aucAddr1: "MACSTR"\n", MAC2STR(prWlanMacHeader->aucAddr1)));
			DBGLOG(RSN, INFO, ("nicRx: aucAddr2: "MACSTR"\n", MAC2STR(prWlanMacHeader->aucAddr2)));
			prStaRec = cnmGetStaRecByAddress(prAdapter, NETWORK_TYPE_AIS_INDEX, prWlanMacHeader->aucAddr2);
			if(prStaRec != NULL && prStaRec->ucStaState == STA_STATE_3)
			{ 			   
				DBGLOG(RSN, INFO, ("Ignore Deauth for Rx Class 3 error!\n"));
			} else {
				  /* receive packets without StaRec */
				prSwRfb->pvHeader = (P_WLAN_MAC_HEADER_T)&prEvent->aucBuffer[0];
				if (WLAN_STATUS_SUCCESS == authSendDeauthFrame(prAdapter,
				                                           NULL,
				                                           prSwRfb,
				                                           REASON_CODE_CLASS_3_ERR,
				                                           (PFN_TX_DONE_HANDLER)NULL)) {
					DBGLOG(RSN, INFO, ("Send Deauth Error\n"));
				}
			}

			DBGLOG(RSN, INFO, ("FW want to send Deauth for Rx Class 3 error!\n"));
		}
    	  break;

#if CFG_SUPPORT_RDD_TEST_MODE
    case EVENT_ID_UPDATE_RDD_STATUS:
        {
            P_EVENT_RDD_STATUS_T prEventRddStatus;

            prEventRddStatus = (P_EVENT_RDD_STATUS_T) (prEvent->aucBuffer);

            prAdapter->ucRddStatus = prEventRddStatus->ucRddStatus;
        }

        break;
#endif

#if CFG_SUPPORT_BCM && CFG_SUPPORT_BCM_BWCS
    case EVENT_ID_UPDATE_BWCS_STATUS:
        {
            P_PTA_IPC_T prEventBwcsStatus;

            prEventBwcsStatus = (P_PTA_IPC_T) (prEvent->aucBuffer);

#if CFG_SUPPORT_BCM_BWCS_DEBUG
            printk(KERN_INFO DRV_NAME "BCM BWCS Event: %02x%02x%02x%02x\n", prEventBwcsStatus->u.aucBTPParams[0],
                prEventBwcsStatus->u.aucBTPParams[1],
                prEventBwcsStatus->u.aucBTPParams[2],
                prEventBwcsStatus->u.aucBTPParams[3]);

            printk(KERN_INFO DRV_NAME "BCM BWCS Event: aucBTPParams[0] = %02x, aucBTPParams[1] = %02x, aucBTPParams[2] = %02x, aucBTPParams[3] = %02x\n",
                prEventBwcsStatus->u.aucBTPParams[0],
                prEventBwcsStatus->u.aucBTPParams[1],
                prEventBwcsStatus->u.aucBTPParams[2],
                prEventBwcsStatus->u.aucBTPParams[3]);
#endif

            kalIndicateStatusAndComplete(prAdapter->prGlueInfo,
                WLAN_STATUS_BWCS_UPDATE,
                (PVOID) prEventBwcsStatus,
                sizeof(PTA_IPC_T));
        }

        break;

    case EVENT_ID_UPDATE_BCM_DEBUG:
        {
            P_PTA_IPC_T prEventBwcsStatus;

            prEventBwcsStatus = (P_PTA_IPC_T) (prEvent->aucBuffer);

#if CFG_SUPPORT_BCM_BWCS_DEBUG
            printk(KERN_INFO DRV_NAME "BCM FW status: %02x%02x%02x%02x\n", prEventBwcsStatus->u.aucBTPParams[0],
                prEventBwcsStatus->u.aucBTPParams[1],
                prEventBwcsStatus->u.aucBTPParams[2],
                prEventBwcsStatus->u.aucBTPParams[3]);

            printk(KERN_INFO DRV_NAME "BCM FW status: aucBTPParams[0] = %02x, aucBTPParams[1] = %02x, aucBTPParams[2] = %02x, aucBTPParams[3] = %02x\n",
                prEventBwcsStatus->u.aucBTPParams[0],
                prEventBwcsStatus->u.aucBTPParams[1],
                prEventBwcsStatus->u.aucBTPParams[2],
                prEventBwcsStatus->u.aucBTPParams[3]);
#endif
        }

        break;
#endif

	case EVENT_ID_DEBUG_CODE: /* only for debug */
	{
		UINT_32 u4CodeId;
		printk("[wlan-fw] function sequence: ");
		for(u4CodeId=0; u4CodeId<1000; u4CodeId++)
			printk("%d ", prEvent->aucBuffer[u4CodeId]);
		printk("\n\n");
	}
		break;

    case EVENT_ID_RFTEST_READY:

        /* command response handling */
        prCmdInfo = nicGetPendingCmdInfo(prAdapter, prEvent->ucSeqNum);

        if(prCmdInfo != NULL) {
            if (prCmdInfo->pfCmdDoneHandler) {
                prCmdInfo->pfCmdDoneHandler(prAdapter, prCmdInfo, prEvent->aucBuffer);
            }
            else if(prCmdInfo->fgIsOid) {
                kalOidComplete(prAdapter->prGlueInfo, prCmdInfo->fgSetQuery, 0, WLAN_STATUS_SUCCESS);
            }

            // return prCmdInfo
            cmdBufFreeCmdInfo(prAdapter, prCmdInfo);
        }

        break;
		
    case EVENT_ID_GSCAN_SCAN_AVAILABLE:
    case EVENT_ID_GSCAN_CAPABILITY:
    case EVENT_ID_GSCAN_SCAN_COMPLETE:      
    case EVENT_ID_GSCAN_FULL_RESULT:  
    case EVENT_ID_GSCAN_SIGNIFICANT_CHANGE:
    case EVENT_ID_GSCAN_GEOFENCE_FOUND: 
        nicRxProcessGSCNEvent( prAdapter,prSwRfb);
         break;    
         
    case EVENT_ID_GSCAN_RESULT: 
    {

        UINT_8 realnum = 0;
        DBGLOG(SCN, TRACE, ("nicRxProcessGSCNEvent  -----> \n"));
        realnum = nicRxProcessGSCNEvent( prAdapter,prSwRfb);
        DBGLOG(SCN, TRACE, ("nicRxProcessGSCNEvent  <----- \n"));

        
#if 0 /* workaround for FW events cnt mis-match with the actual reqirements from wifi_hal*/
        if(g_GetResultsCmdCnt == 0){
            DBGLOG(SCN, INFO, ("FW report events more than the wifi_hal asked number, buffer the results for future reporting \n"));                               
            UINT_8 i = 0;
            for(i=0; i < MAX_BUFFERED_GSCN_RESULTS; i++){
            #if 1
                if(!g_arGscanResultsIndicateNumber[i]){
                    printk("found available index[%d] to insert results number [%d] into buffer \r\n", i, realnum);

                    g_arGscnResultsTempBuffer[i] = prSwRfb;
                    g_arGscanResultsIndicateNumber[i] = realnum;
                    g_GetResultsBufferedCnt++;
                    fgKeepprSwRfb = TRUE;                   
                    printk("results buffered in index[%d] \r\n", i );
                    break;
                }
            #endif
            }
            if(i == MAX_BUFFERED_GSCN_RESULTS){           
                printk("the buffer for Gscn results is full(all valid), no extra space to buffer this result\r\n");
            }
        }
        else if(g_GetResultsCmdCnt > 0) {
            DBGLOG(SCN, INFO, ("FW report events match the wifi_hal asked number \n"));          
            g_GetResultsCmdCnt--;
        }
        else{
            DBGLOG(SCN, INFO, ("g_GetResultsCmdCnt < 0 ??? unexpected case  \n"));
        }
#endif
        /* end of workaround */

    }   
        break;  
#if CFG_SUPPORT_BATCH_SCAN
	case EVENT_ID_BATCH_RESULT:
		DBGLOG(SCN, TRACE, ("Got EVENT_ID_BATCH_RESULT"));

		/* command response handling */
		prCmdInfo = nicGetPendingCmdInfo(prAdapter, prEvent->ucSeqNum);

		if (prCmdInfo != NULL) {
			if (prCmdInfo->pfCmdDoneHandler) {
				prCmdInfo->pfCmdDoneHandler(prAdapter, prCmdInfo,
								prEvent->aucBuffer);
			} else if (prCmdInfo->fgIsOid) {
				kalOidComplete(prAdapter->prGlueInfo, prCmdInfo->fgSetQuery, 0,
						   WLAN_STATUS_SUCCESS);
			}
			/* return prCmdInfo */
			cmdBufFreeCmdInfo(prAdapter, prCmdInfo);
		}

		break;
#endif /* CFG_SUPPORT_BATCH_SCAN */

#if (CFG_SUPPORT_TDLS == 1)
	case EVENT_ID_TDLS:
		TdlsexEventHandle(\
			prAdapter->prGlueInfo,
			(UINT8 *)prEvent->aucBuffer,
			(UINT32)(prEvent->u2PacketLen-8));
		break;
#endif /* CFG_SUPPORT_TDLS */

#if (CFG_SUPPORT_STATISTICS == 1)
	case EVENT_ID_STATS_ENV:
		statsEventHandle(\
			prAdapter->prGlueInfo,
			(UINT8 *)prEvent->aucBuffer,
			(UINT32)(prEvent->u2PacketLen-8));
		break;
#endif /* CFG_SUPPORT_STATISTICS */

    case EVENT_ID_ACCESS_REG:
    case EVENT_ID_NIC_CAPABILITY:
    case EVENT_ID_BASIC_CONFIG:
    case EVENT_ID_MAC_MCAST_ADDR:
    case EVENT_ID_ACCESS_EEPROM:
    case EVENT_ID_TEST_STATUS:
#if CFG_SUPPORT_BUILD_DATE_CODE
    case EVENT_ID_BUILD_DATE_CODE:
#endif
    case EVENT_ID_GET_AIS_BSS_INFO:
    default:
        prCmdInfo = nicGetPendingCmdInfo(prAdapter, prEvent->ucSeqNum);

        if(prCmdInfo != NULL) {
            if (prCmdInfo->pfCmdDoneHandler) {
                prCmdInfo->pfCmdDoneHandler(prAdapter, prCmdInfo, prEvent->aucBuffer);
            }
            else if(prCmdInfo->fgIsOid) {
                kalOidComplete(prAdapter->prGlueInfo, prCmdInfo->fgSetQuery, 0, WLAN_STATUS_SUCCESS);
            }

            // return prCmdInfo
            cmdBufFreeCmdInfo(prAdapter, prCmdInfo);
        }

        break;
    }

    nicRxReturnRFB(prAdapter, prSwRfb);
}


/*----------------------------------------------------------------------------*/
/*!
* @brief nicRxProcessMgmtPacket is used to dispatch management frames
*        to corresponding modules
*
* @param prAdapter Pointer to the Adapter structure.
* @param prSWRfb the RFB to receive rx data
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
nicRxProcessMgmtPacket (
    IN P_ADAPTER_T    prAdapter,
    IN OUT P_SW_RFB_T prSwRfb
    )
{
    UINT_8 ucSubtype;
#if CFG_SUPPORT_802_11W
    BOOLEAN   fgMfgDrop = FALSE;
#endif
    ASSERT(prAdapter);
    ASSERT(prSwRfb);

    nicRxFillRFB(prAdapter, prSwRfb);

    ucSubtype = (*(PUINT_8)(prSwRfb->pvHeader) & MASK_FC_SUBTYPE )>> OFFSET_OF_FC_SUBTYPE;

#if 0 //CFG_RX_PKTS_DUMP
    {
        P_HIF_RX_HEADER_T   prHifRxHdr;
        UINT_16 u2TxFrameCtrl;

        prHifRxHdr = prSwRfb->prHifRxHdr;
        u2TxFrameCtrl = (*(PUINT_8)(prSwRfb->pvHeader) & MASK_FRAME_TYPE);
        //if (prAdapter->rRxCtrl.u4RxPktsDumpTypeMask & BIT(HIF_RX_PKT_TYPE_MANAGEMENT)) {
        //    if (u2TxFrameCtrl == MAC_FRAME_BEACON ||
        //    	  u2TxFrameCtrl == MAC_FRAME_PROBE_RSP) {

                DBGLOG(RX, INFO, ("QM RX MGT: net %u sta idx %u wlan idx %u ssn %u ptype %u subtype %u 11 %u\n",
                    (UINT_32)HIF_RX_HDR_GET_NETWORK_IDX(prHifRxHdr),
                    prHifRxHdr->ucStaRecIdx,
                    prSwRfb->ucWlanIdx,
                    (UINT_32)HIF_RX_HDR_GET_SN(prHifRxHdr),  /* The new SN of the frame */
                    prSwRfb->ucPacketType,
                    ucSubtype,
                    HIF_RX_HDR_GET_80211_FLAG(prHifRxHdr)));

                //DBGLOG_MEM8(SW4, TRACE, (PUINT_8)prSwRfb->pvHeader, prSwRfb->u2PacketLen);
        //    }
        //}
    }
#endif

    if ((prAdapter->fgTestMode == FALSE) &&
		(prAdapter->prGlueInfo->fgIsRegistered == TRUE))
	{
#if CFG_MGMT_FRAME_HANDLING
#if CFG_SUPPORT_802_11W
        fgMfgDrop = rsnCheckRxMgmt(prAdapter, prSwRfb, ucSubtype);
        if (fgMfgDrop) {
            #if DBG
            LOG_FUNC("QM RX MGT: Drop Unprotected Mgmt frame!!!\n");
            #endif
            nicRxReturnRFB(prAdapter, prSwRfb);
            RX_INC_CNT(prRxCtrl, RX_DROP_TOTAL_COUNT);
            return;
        }
        else
#endif
        if(apfnProcessRxMgtFrame[ucSubtype]) {
            switch(apfnProcessRxMgtFrame[ucSubtype](prAdapter, prSwRfb)){
            case WLAN_STATUS_PENDING:
                return;
            case WLAN_STATUS_SUCCESS:
            case WLAN_STATUS_FAILURE:
                break;

            default:
                DBGLOG(RX, WARN, ("Unexpected MMPDU(0x%02X) returned with abnormal status\n", ucSubtype));
                break;
            }
        }
#endif
    }

    nicRxReturnRFB(prAdapter, prSwRfb);
}

/*----------------------------------------------------------------------------*/
/*!
* @brief nicProcessRFBs is used to process RFBs in the rReceivedRFBList queue.
*
* @param prAdapter Pointer to the Adapter structure.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
nicRxProcessRFBs (
    IN  P_ADAPTER_T prAdapter
    )
{
    P_RX_CTRL_T prRxCtrl;
    P_SW_RFB_T prSwRfb = (P_SW_RFB_T)NULL;
    KAL_SPIN_LOCK_DECLARATION();

    DEBUGFUNC("nicRxProcessRFBs");

    ASSERT(prAdapter);

    prRxCtrl = &prAdapter->rRxCtrl;
    ASSERT(prRxCtrl);

    prRxCtrl->ucNumIndPacket = 0;
    prRxCtrl->ucNumRetainedPacket = 0;

    do {
        KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_RX_QUE);
        QUEUE_REMOVE_HEAD(&prRxCtrl->rReceivedRfbList, prSwRfb, P_SW_RFB_T);
        KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_RX_QUE);

        if (prSwRfb){
            switch(prSwRfb->ucPacketType){
                case HIF_RX_PKT_TYPE_DATA:
                    nicRxProcessDataPacket(prAdapter, prSwRfb);
                    break;

                case HIF_RX_PKT_TYPE_EVENT:
                    nicRxProcessEventPacket(prAdapter, prSwRfb);
                    break;

                case HIF_RX_PKT_TYPE_TX_LOOPBACK:
#if (CONF_HIF_LOOPBACK_AUTO == 1)
{
                    extern void
                        kalDevLoopbkRxHandle(
                            IN P_ADAPTER_T    prAdapter,
                            IN OUT P_SW_RFB_T prSwRfb
                            );
                    kalDevLoopbkRxHandle(prAdapter, prSwRfb);
                    nicRxReturnRFB(prAdapter, prSwRfb);
}
#else
                    DBGLOG(RX, ERROR, ("ucPacketType = %d\n", prSwRfb->ucPacketType));
#endif /* CONF_HIF_LOOPBACK_AUTO */
                    break;

                case HIF_RX_PKT_TYPE_MANAGEMENT:
                    nicRxProcessMgmtPacket(prAdapter, prSwRfb);
                    break;

                default:
                    RX_INC_CNT(prRxCtrl, RX_TYPE_ERR_DROP_COUNT);
                    RX_INC_CNT(prRxCtrl, RX_DROP_TOTAL_COUNT);
                    DBGLOG(RX, ERROR, ("ucPacketType = %d\n", prSwRfb->ucPacketType));
                    nicRxReturnRFB(prAdapter, prSwRfb); /* need to free it */
                    break;
            }
			
		do {
			P_WIFI_EVENT_T prEvent;
			PUINT_8 pvHeader = (PUINT_8)(prSwRfb->pvHeader);
			UINT_8 ucSubtype;
			UINT_16 u2Temp = 0;
			P_HIF_RX_HEADER_T prHifRxHdr = prSwRfb->prHifRxHdr;
				
			if (!kalIsWakeupByWlan(prAdapter))
				break;

			switch (prSwRfb->ucPacketType) {
			case HIF_RX_PKT_TYPE_DATA:
				u2Temp = (pvHeader[ETH_TYPE_LEN_OFFSET] << 8) |
									(pvHeader[ETH_TYPE_LEN_OFFSET + 1]);
					
				if (u2Temp == ETH_P_IP) {
					u2Temp = *(UINT_16 *) &pvHeader[ETH_HLEN + 4];
					DBGLOG(INIT, INFO, ("IP Packet from:%d.%d.%d.%d, IP ID 0x%04x wakeup host\n",
							pvHeader[ETH_HLEN + 12], pvHeader[ETH_HLEN + 13],
							pvHeader[ETH_HLEN + 14], pvHeader[ETH_HLEN + 15], u2Temp));
				} else
					DBGLOG(INIT, INFO, ("Data Packet, EthType 0x%04x wakeup host\n", u2Temp));
				break;
			case HIF_RX_PKT_TYPE_EVENT:
				prEvent = (P_WIFI_EVENT_T) prSwRfb->pucRecvBuff;
				DBGLOG(INIT, INFO, ("Event 0x%02x wakeup host\n", prEvent->ucEID));
				break;
			case HIF_RX_PKT_TYPE_MANAGEMENT:
				ucSubtype = (*pvHeader & MASK_FC_SUBTYPE ) >> OFFSET_OF_FC_SUBTYPE;
				DBGLOG(INIT, INFO, ("MGMT frame subtype: %d Seq %u wakeup host\n",
					ucSubtype, (UINT_32)HIF_RX_HDR_GET_SN(prHifRxHdr)));
				break;
			default:
				DBGLOG(INIT, INFO, ("Unknow Packet %d wakeup host\n", prSwRfb->ucPacketType));
				break;
			}
		} while (FALSE);
        }
        else {
            break;
        }
    }while(TRUE);

     if (prRxCtrl->ucNumIndPacket > 0) {
        RX_ADD_CNT(prRxCtrl, RX_DATA_INDICATION_COUNT, prRxCtrl->ucNumIndPacket);
        RX_ADD_CNT(prRxCtrl, RX_DATA_RETAINED_COUNT, prRxCtrl->ucNumRetainedPacket);

        //DBGLOG(RX, INFO, ("%d packets indicated, Retained cnt = %d\n",
        //    prRxCtrl->ucNumIndPacket, prRxCtrl->ucNumRetainedPacket));
    #if CFG_NATIVE_802_11
        kalRxIndicatePkts(prAdapter->prGlueInfo, (UINT_32)prRxCtrl->ucNumIndPacket, (UINT_32)prRxCtrl->ucNumRetainedPacket);
    #else
        kalRxIndicatePkts(prAdapter->prGlueInfo, prRxCtrl->apvIndPacket, (UINT_32)prRxCtrl->ucNumIndPacket);
    #endif
    }

} /* end of nicRxProcessRFBs() */


#if !CFG_SDIO_INTR_ENHANCE
/*----------------------------------------------------------------------------*/
/*!
* @brief Read the rx data from data port and setup RFB
*
* @param prAdapter pointer to the Adapter handler
* @param prSWRfb the RFB to receive rx data
*
* @retval WLAN_STATUS_SUCCESS: SUCCESS
* @retval WLAN_STATUS_FAILURE: FAILURE
*
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
nicRxReadBuffer (
    IN P_ADAPTER_T prAdapter,
    IN OUT P_SW_RFB_T prSwRfb
    )
{
    P_RX_CTRL_T prRxCtrl;
    PUINT_8 pucBuf;
    P_HIF_RX_HEADER_T prHifRxHdr;
    UINT_32 u4PktLen = 0, u4ReadBytes;
    WLAN_STATUS u4Status = WLAN_STATUS_SUCCESS;
    BOOLEAN fgResult = TRUE;
    UINT_32 u4RegValue;
    UINT_32 rxNum;

    DEBUGFUNC("nicRxReadBuffer");

    ASSERT(prAdapter);
    ASSERT(prSwRfb);

    prRxCtrl = &prAdapter->rRxCtrl;
    ASSERT(prRxCtrl);

    pucBuf = prSwRfb->pucRecvBuff;
    prHifRxHdr = prSwRfb->prHifRxHdr;
    ASSERT(pucBuf);
    DBGLOG(RX, TRACE, ("pucBuf= 0x%x, prHifRxHdr= 0x%x\n", pucBuf, prHifRxHdr));

    do {
        /* Read the RFB DW length and packet length */
        HAL_MCR_RD(prAdapter, MCR_WRPLR, &u4RegValue);
        if (!fgResult) {
            DBGLOG(RX, ERROR, ("Read RX Packet Lentgh Error\n"));
            return WLAN_STATUS_FAILURE;
        }

        //20091021 move the line to get the HIF RX header (for RX0/1)
        if(u4RegValue == 0) {
            DBGLOG(RX, ERROR, ("No RX packet\n"));
            return WLAN_STATUS_FAILURE;
        }

        u4PktLen = u4RegValue & BITS(0, 15);
        if(u4PktLen != 0) {
            rxNum = 0;
        }
        else {
            rxNum = 1;
            u4PktLen = (u4RegValue & BITS(16, 31)) >> 16;
        }

        DBGLOG(RX, TRACE, ("RX%d: u4PktLen = %d\n", rxNum, u4PktLen));

        //4 <4> Read Entire RFB and packet, include HW appended DW (Checksum Status)
        u4ReadBytes = ALIGN_4(u4PktLen) + 4;
        HAL_READ_RX_PORT(prAdapter, rxNum, u4ReadBytes, pucBuf, CFG_RX_MAX_PKT_SIZE);

        //20091021 move the line to get the HIF RX header
        //u4PktLen = (UINT_32)prHifRxHdr->u2PacketLen;
        if (u4PktLen != (UINT_32)prHifRxHdr->u2PacketLen) {
           DBGLOG(RX, ERROR, ("Read u4PktLen = %d, prHifRxHdr->u2PacketLen: %d\n",
                                u4PktLen, prHifRxHdr->u2PacketLen));
    #if DBG
            dumpMemory8((PUINT_8)prHifRxHdr, (prHifRxHdr->u2PacketLen > 4096) ? 4096 : prHifRxHdr->u2PacketLen);
    #endif
            ASSERT(0);
        }
        /* u4PktLen is byte unit, not inlude HW appended DW */

        prSwRfb->ucPacketType = (UINT_8)(prHifRxHdr->u2PacketType & HIF_RX_HDR_PACKET_TYPE_MASK);
        DBGLOG(RX, TRACE, ("ucPacketType = %d\n", prSwRfb->ucPacketType));

        prSwRfb->ucStaRecIdx = (UINT_8)(prHifRxHdr->ucStaRecIdx);

        /* fgResult will be updated in MACRO */
        if (!fgResult) {
            return WLAN_STATUS_FAILURE;
        }

        DBGLOG(RX, TRACE, ("Dump RX buffer, length = 0x%x\n", u4ReadBytes));
        DBGLOG_MEM8(RX, TRACE, pucBuf, u4ReadBytes);
    }while(FALSE);

    return u4Status;
}


/*----------------------------------------------------------------------------*/
/*!
* @brief Read frames from the data port, fill RFB
*        and put each frame into the rReceivedRFBList queue.
*
* @param prAdapter   Pointer to the Adapter structure.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
nicRxReceiveRFBs (
    IN P_ADAPTER_T  prAdapter
    )
{
    P_RX_CTRL_T prRxCtrl;
    P_SW_RFB_T prSwRfb = (P_SW_RFB_T)NULL;
    P_HIF_RX_HEADER_T prHifRxHdr;

    UINT_32 u4HwAppendDW;

    KAL_SPIN_LOCK_DECLARATION();

    DEBUGFUNC("nicRxReceiveRFBs");

    ASSERT(prAdapter);

    prRxCtrl = &prAdapter->rRxCtrl;
    ASSERT(prRxCtrl);

    do {
        KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_RX_QUE);
        QUEUE_REMOVE_HEAD(&prRxCtrl->rFreeSwRfbList, prSwRfb, P_SW_RFB_T);
        KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_RX_QUE);

        if (!prSwRfb) {
            DBGLOG(RX, TRACE, ("No More RFB\n"));
            break;
        }

        // need to consider
        if (nicRxReadBuffer(prAdapter, prSwRfb) == WLAN_STATUS_FAILURE) {
            DBGLOG(RX, TRACE, ("halRxFillRFB failed\n"));
            nicRxReturnRFB(prAdapter, prSwRfb);
            break;
        }

        KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_RX_QUE);
        QUEUE_INSERT_TAIL(&prRxCtrl->rReceivedRfbList, &prSwRfb->rQueEntry);
        RX_INC_CNT(prRxCtrl, RX_MPDU_TOTAL_COUNT);
        KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_RX_QUE);

        prHifRxHdr = prSwRfb->prHifRxHdr;
        u4HwAppendDW = *((PUINT_32)((ULONG)prHifRxHdr +
            (UINT_32)(ALIGN_4(prHifRxHdr->u2PacketLen))));
        DBGLOG(RX, TRACE, ("u4HwAppendDW = 0x%x\n", u4HwAppendDW));
        DBGLOG(RX, TRACE, ("u2PacketLen = 0x%x\n", prHifRxHdr->u2PacketLen));
      }
//    while (RX_STATUS_TEST_MORE_FLAG(u4HwAppendDW));
    while (FALSE);

    return;

} /* end of nicReceiveRFBs() */

#else
/*----------------------------------------------------------------------------*/
/*!
* @brief Read frames from the data port, fill RFB
*        and put each frame into the rReceivedRFBList queue.
*
* @param prAdapter      Pointer to the Adapter structure.
* @param u4DataPort     Specify which port to read
* @param u2RxLength     Specify to the the rx packet length in Byte.
* @param prSwRfb        the RFB to receive rx data.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/

WLAN_STATUS
nicRxEnhanceReadBuffer (
    IN P_ADAPTER_T prAdapter,
    IN UINT_32      u4DataPort,
    IN UINT_16      u2RxLength,
    IN OUT P_SW_RFB_T prSwRfb
    )
{
    P_RX_CTRL_T prRxCtrl;
    PUINT_8 pucBuf;
    P_HIF_RX_HEADER_T prHifRxHdr;
    UINT_32 u4PktLen = 0;
    WLAN_STATUS u4Status = WLAN_STATUS_FAILURE;
    BOOLEAN fgResult = TRUE;

    DEBUGFUNC("nicRxEnhanceReadBuffer");

    ASSERT(prAdapter);
    ASSERT(prSwRfb);

    prRxCtrl = &prAdapter->rRxCtrl;
    ASSERT(prRxCtrl);

    pucBuf = prSwRfb->pucRecvBuff;
    ASSERT(pucBuf);

    prHifRxHdr = prSwRfb->prHifRxHdr;
    ASSERT(prHifRxHdr);

    //DBGLOG(RX, TRACE, ("u2RxLength = %d\n", u2RxLength));

    do {
        //4 <1> Read RFB frame from MCR_WRDR0, include HW appended DW
        HAL_READ_RX_PORT(prAdapter,
                         u4DataPort,
                         ALIGN_4(u2RxLength + HIF_RX_HW_APPENDED_LEN),
                         pucBuf,
                         CFG_RX_MAX_PKT_SIZE);

        if (!fgResult) {
            DBGLOG(RX, ERROR, ("Read RX Packet Lentgh Error\n"));
            break;
        }

        u4PktLen = (UINT_32)(prHifRxHdr->u2PacketLen);
        //DBGLOG(RX, TRACE, ("u4PktLen = %d\n", u4PktLen));

        prSwRfb->ucPacketType = (UINT_8)(prHifRxHdr->u2PacketType & HIF_RX_HDR_PACKET_TYPE_MASK);
        //DBGLOG(RX, TRACE, ("ucPacketType = %d\n", prSwRfb->ucPacketType));

        prSwRfb->ucStaRecIdx = (UINT_8)(prHifRxHdr->ucStaRecIdx);

        //4 <2> if the RFB dw size or packet size is zero
        if (u4PktLen == 0) {
            DBGLOG(RX, ERROR, ("Packet Length = %u\n", u4PktLen));
            ASSERT(0);
            break;
        }

        //4 <3> if the packet is too large or too small
        if (u4PktLen > CFG_RX_MAX_PKT_SIZE) {
            DBGLOG(RX, TRACE, ("Read RX Packet Lentgh Error (%u)\n", u4PktLen));
            ASSERT(0);
            break;
        }

        u4Status = WLAN_STATUS_SUCCESS;
    }
    while (FALSE);

    DBGLOG_MEM8(RX, TRACE, pucBuf, ALIGN_4(u2RxLength + HIF_RX_HW_APPENDED_LEN));
    return u4Status;
}


/*----------------------------------------------------------------------------*/
/*!
* @brief Read frames from the data port for SDIO
*        I/F, fill RFB and put each frame into the rReceivedRFBList queue.
*
* @param prAdapter      Pointer to the Adapter structure.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
nicRxSDIOReceiveRFBs (
    IN  P_ADAPTER_T prAdapter
    )
{
    P_SDIO_CTRL_T prSDIOCtrl;
    P_RX_CTRL_T prRxCtrl;
    P_SW_RFB_T prSwRfb = (P_SW_RFB_T)NULL;
    UINT_32 i, rxNum;
    UINT_16 u2RxPktNum, u2RxLength = 0, u2Tmp = 0;
    KAL_SPIN_LOCK_DECLARATION();

    DEBUGFUNC("nicRxSDIOReceiveRFBs");

    ASSERT(prAdapter);

    prSDIOCtrl = prAdapter->prSDIOCtrl;
    ASSERT(prSDIOCtrl);

    prRxCtrl = &prAdapter->rRxCtrl;
    ASSERT(prRxCtrl);

    for (rxNum = 0 ; rxNum < 2 ; rxNum++) {
        u2RxPktNum = (rxNum == 0 ? prSDIOCtrl->rRxInfo.u.u2NumValidRx0Len : prSDIOCtrl->rRxInfo.u.u2NumValidRx1Len);

        if(u2RxPktNum == 0) {
            continue;
        }

        for (i = 0; i < u2RxPktNum; i++) {
            if(rxNum == 0) {
                HAL_READ_RX_LENGTH(prAdapter, &u2RxLength, &u2Tmp);
            }
            else if(rxNum == 1) {
                HAL_READ_RX_LENGTH(prAdapter, &u2Tmp, &u2RxLength);
            }

            if (!u2RxLength) {
                break;
            }


            KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_RX_QUE);
            QUEUE_REMOVE_HEAD(&prRxCtrl->rFreeSwRfbList, prSwRfb, P_SW_RFB_T);
            KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_RX_QUE);

            if (!prSwRfb) {
                DBGLOG(RX, TRACE, ("No More RFB\n"));
                break;
            }
            ASSERT(prSwRfb);

            if (nicRxEnhanceReadBuffer(prAdapter, rxNum, u2RxLength, prSwRfb) == WLAN_STATUS_FAILURE) {
                DBGLOG(RX, TRACE, ("nicRxEnhanceRxReadBuffer failed\n"));
                nicRxReturnRFB(prAdapter, prSwRfb);
                break;
            }

            //prSDIOCtrl->au4RxLength[i] = 0;

            KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_RX_QUE);
            QUEUE_INSERT_TAIL(&prRxCtrl->rReceivedRfbList, &prSwRfb->rQueEntry);
            RX_INC_CNT(prRxCtrl, RX_MPDU_TOTAL_COUNT);
            KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_RX_QUE);
        }
    }

    prSDIOCtrl->rRxInfo.u.u2NumValidRx0Len = 0;
    prSDIOCtrl->rRxInfo.u.u2NumValidRx1Len = 0;

    return;
}/* end of nicRxSDIOReceiveRFBs() */

#endif /* CFG_SDIO_INTR_ENHANCE */



#if CFG_SDIO_RX_AGG
/*----------------------------------------------------------------------------*/
/*!
* @brief Read frames from the data port for SDIO with Rx aggregation enabled
*        I/F, fill RFB and put each frame into the rReceivedRFBList queue.
*
* @param prAdapter      Pointer to the Adapter structure.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
nicRxSDIOAggReceiveRFBs (
    IN  P_ADAPTER_T prAdapter
    )
{
    P_ENHANCE_MODE_DATA_STRUCT_T prEnhDataStr;
    P_RX_CTRL_T prRxCtrl;
    P_SDIO_CTRL_T prSDIOCtrl;
    P_SW_RFB_T prSwRfb = (P_SW_RFB_T)NULL;
    UINT_32 u4RxLength;
    UINT_32 i, rxNum;
    UINT_32 u4RxAggCount = 0, u4RxAggLength = 0;
    UINT_32 u4RxAvailAggLen, u4CurrAvailFreeRfbCnt;
    PUINT_8 pucSrcAddr;
    P_HIF_RX_HEADER_T prHifRxHdr;
    BOOLEAN fgResult = TRUE;
    BOOLEAN fgIsRxEnhanceMode;
    UINT_16 u2RxPktNum;
#if CFG_SDIO_RX_ENHANCE
    UINT_32 u4MaxLoopCount = CFG_MAX_RX_ENHANCE_LOOP_COUNT;
#endif

    KAL_SPIN_LOCK_DECLARATION();

    DEBUGFUNC("nicRxSDIOAggReceiveRFBs");

    ASSERT(prAdapter);
    prEnhDataStr = prAdapter->prSDIOCtrl;
    prRxCtrl = &prAdapter->rRxCtrl;
    prSDIOCtrl = prAdapter->prSDIOCtrl;

#if CFG_SDIO_RX_ENHANCE
    fgIsRxEnhanceMode = TRUE;
#else
    fgIsRxEnhanceMode = FALSE;
#endif

    do {
#if CFG_SDIO_RX_ENHANCE
        /* to limit maximum loop for RX */
        u4MaxLoopCount--;
        if (u4MaxLoopCount == 0) {
            break;
        }
#endif

        if(prEnhDataStr->rRxInfo.u.u2NumValidRx0Len == 0 &&
                prEnhDataStr->rRxInfo.u.u2NumValidRx1Len == 0) {
            break;
        }

        for(rxNum = 0 ; rxNum < 2 ; rxNum++) {
            u2RxPktNum = (rxNum == 0 ? prEnhDataStr->rRxInfo.u.u2NumValidRx0Len : prEnhDataStr->rRxInfo.u.u2NumValidRx1Len);

            // if this assertion happened, it is most likely a F/W bug
            ASSERT(u2RxPktNum <= 16);

            if (u2RxPktNum > 16)
            	  continue;

            if(u2RxPktNum == 0)
                continue;

    #if CFG_HIF_STATISTICS
            prRxCtrl->u4TotalRxAccessNum++;
            prRxCtrl->u4TotalRxPacketNum += u2RxPktNum;
    #endif

            u4CurrAvailFreeRfbCnt = prRxCtrl->rFreeSwRfbList.u4NumElem;

            // if SwRfb is not enough, abort reading this time
             if(u4CurrAvailFreeRfbCnt < u2RxPktNum) {
    #if CFG_HIF_RX_STARVATION_WARNING
                DbgPrint("FreeRfb is not enough: %d available, need %d\n", u4CurrAvailFreeRfbCnt, u2RxPktNum);
                DbgPrint("Queued Count: %d / Dequeud Count: %d\n", prRxCtrl->u4QueuedCnt, prRxCtrl->u4DequeuedCnt);
    #endif
                continue;
            }

#if CFG_SDIO_RX_ENHANCE
            u4RxAvailAggLen = CFG_RX_COALESCING_BUFFER_SIZE - (sizeof(ENHANCE_MODE_DATA_STRUCT_T) + 4/* extra HW padding */);
#else
            u4RxAvailAggLen = CFG_RX_COALESCING_BUFFER_SIZE;
#endif
            u4RxAggCount = 0;

            for (i = 0; i < u2RxPktNum ; i++) {
                u4RxLength = (rxNum == 0 ?
                        (UINT_32)prEnhDataStr->rRxInfo.u.au2Rx0Len[i] :
                        (UINT_32)prEnhDataStr->rRxInfo.u.au2Rx1Len[i]);

                if (!u4RxLength) {
                    ASSERT(0);
                    break;
                }

                if (ALIGN_4(u4RxLength + HIF_RX_HW_APPENDED_LEN) < u4RxAvailAggLen) {
                    if (u4RxAggCount < u4CurrAvailFreeRfbCnt) {
                        u4RxAvailAggLen -= ALIGN_4(u4RxLength + HIF_RX_HW_APPENDED_LEN);
                        u4RxAggCount++;
                    }
                    else {
                        // no FreeSwRfb for rx packet
                        ASSERT(0);
                        break;
                    }
                }
                else {
                    // CFG_RX_COALESCING_BUFFER_SIZE is not large enough
                    ASSERT(0);
                    break;
                }
            }

            u4RxAggLength = (CFG_RX_COALESCING_BUFFER_SIZE - u4RxAvailAggLen);
            //DBGLOG(RX, INFO, ("u4RxAggCount = %d, u4RxAggLength = %d\n",
            //            u4RxAggCount, u4RxAggLength));

            HAL_READ_RX_PORT(prAdapter,
                         rxNum,
                         u4RxAggLength,
                         prRxCtrl->pucRxCoalescingBufPtr,
                         CFG_RX_COALESCING_BUFFER_SIZE);
            if (!fgResult) {
                DBGLOG(RX, ERROR, ("Read RX Agg Packet Error\n"));
                continue;
            }

            pucSrcAddr = prRxCtrl->pucRxCoalescingBufPtr;
            for (i = 0; i < u4RxAggCount; i++) {
                UINT_16 u2PktLength;

                u2PktLength = (rxNum == 0 ?
                        prEnhDataStr->rRxInfo.u.au2Rx0Len[i] :
                        prEnhDataStr->rRxInfo.u.au2Rx1Len[i]);

                KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_RX_QUE);
                QUEUE_REMOVE_HEAD(&prRxCtrl->rFreeSwRfbList, prSwRfb, P_SW_RFB_T);
                KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_RX_QUE);

                ASSERT(prSwRfb);
                kalMemCopy(prSwRfb->pucRecvBuff, pucSrcAddr,
                        ALIGN_4(u2PktLength + HIF_RX_HW_APPENDED_LEN));

				/* record the rx time */
				STATS_RX_ARRIVE_TIME_RECORD(prSwRfb); /* ms */

                prHifRxHdr = prSwRfb->prHifRxHdr;
                ASSERT(prHifRxHdr);

                prSwRfb->ucPacketType = (UINT_8)(prHifRxHdr->u2PacketType & HIF_RX_HDR_PACKET_TYPE_MASK);
                //DBGLOG(RX, TRACE, ("ucPacketType = %d\n", prSwRfb->ucPacketType));

                prSwRfb->ucStaRecIdx = (UINT_8)(prHifRxHdr->ucStaRecIdx);

                KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_RX_QUE);
                QUEUE_INSERT_TAIL(&prRxCtrl->rReceivedRfbList, &prSwRfb->rQueEntry);
                RX_INC_CNT(prRxCtrl, RX_MPDU_TOTAL_COUNT);
                KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_RX_QUE);

                pucSrcAddr += ALIGN_4(u2PktLength + HIF_RX_HW_APPENDED_LEN);
                //prEnhDataStr->au4RxLength[i] = 0;
            }

#if CFG_SDIO_RX_ENHANCE
            kalMemCopy(prAdapter->prSDIOCtrl, (pucSrcAddr + 4), sizeof(ENHANCE_MODE_DATA_STRUCT_T));

            /* do the same thing what nicSDIOReadIntStatus() does */
            if((prSDIOCtrl->u4WHISR & WHISR_TX_DONE_INT) == 0 &&
                    (prSDIOCtrl->rTxInfo.au4WTSR[0] | prSDIOCtrl->rTxInfo.au4WTSR[1])) {
                prSDIOCtrl->u4WHISR |= WHISR_TX_DONE_INT;
            }

            if((prSDIOCtrl->u4WHISR & BIT(31)) == 0 &&
                    HAL_GET_MAILBOX_READ_CLEAR(prAdapter) == TRUE &&
                    (prSDIOCtrl->u4RcvMailbox0 != 0 || prSDIOCtrl->u4RcvMailbox1 != 0)) {
                prSDIOCtrl->u4WHISR |= BIT(31);
            }

            /* dispatch to interrupt handler with RX bits masked */
            nicProcessIST_impl(prAdapter, prSDIOCtrl->u4WHISR & (~(WHISR_RX0_DONE_INT | WHISR_RX1_DONE_INT)));
#endif
        }

#if !CFG_SDIO_RX_ENHANCE
        prEnhDataStr->rRxInfo.u.u2NumValidRx0Len = 0;
        prEnhDataStr->rRxInfo.u.u2NumValidRx1Len = 0;
#endif
    }
    while ((prEnhDataStr->rRxInfo.u.u2NumValidRx0Len
                || prEnhDataStr->rRxInfo.u.u2NumValidRx1Len)
            && fgIsRxEnhanceMode);

    return;
}
#endif /* CFG_SDIO_RX_AGG */


/*----------------------------------------------------------------------------*/
/*!
* @brief Setup a RFB and allocate the os packet to the RFB
*
* @param prAdapter      Pointer to the Adapter structure.
* @param prSwRfb        Pointer to the RFB
*
* @retval WLAN_STATUS_SUCCESS
* @retval WLAN_STATUS_RESOURCES
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
nicRxSetupRFB (
    IN P_ADAPTER_T prAdapter,
    IN P_SW_RFB_T  prSwRfb
    )
{
    PVOID   pvPacket;
    PUINT_8 pucRecvBuff;

    ASSERT(prAdapter);
    ASSERT(prSwRfb);

    if (!prSwRfb->pvPacket) {
        kalMemZero(prSwRfb, sizeof(SW_RFB_T));
        pvPacket = kalPacketAlloc(prAdapter->prGlueInfo, CFG_RX_MAX_PKT_SIZE,
            &pucRecvBuff);
        if (pvPacket == NULL) {
            return WLAN_STATUS_RESOURCES;
        }

        prSwRfb->pvPacket = pvPacket;
        prSwRfb->pucRecvBuff= (PVOID)pucRecvBuff;
    }
    else {
        kalMemZero(((PUINT_8)prSwRfb + OFFSET_OF(SW_RFB_T, prHifRxHdr)),
            (sizeof(SW_RFB_T)-OFFSET_OF(SW_RFB_T, prHifRxHdr)));
    }

    prSwRfb->prHifRxHdr = (P_HIF_RX_HEADER_T)(prSwRfb->pucRecvBuff);

    return WLAN_STATUS_SUCCESS;

} /* end of nicRxSetupRFB() */


/*----------------------------------------------------------------------------*/
/*!
* @brief This routine is called to put a RFB back onto the "RFB with Buffer" list
*        or "RFB without buffer" list according to pvPacket.
*
* @param prAdapter      Pointer to the Adapter structure.
* @param prSwRfb          Pointer to the RFB
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
nicRxReturnRFB (
    IN P_ADAPTER_T prAdapter,
    IN P_SW_RFB_T  prSwRfb
    )
{
    P_RX_CTRL_T prRxCtrl;
    P_QUE_ENTRY_T prQueEntry;
    KAL_SPIN_LOCK_DECLARATION();

    ASSERT(prAdapter);
    ASSERT(prSwRfb);
    prRxCtrl = &prAdapter->rRxCtrl;
    prQueEntry = &prSwRfb->rQueEntry;

    ASSERT(prQueEntry);

    /* The processing on this RFB is done, so put it back on the tail of
       our list */
    KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_RX_QUE);

    if (prSwRfb->pvPacket) {
        QUEUE_INSERT_TAIL(&prRxCtrl->rFreeSwRfbList, prQueEntry);
    }
    else {
        QUEUE_INSERT_TAIL(&prRxCtrl->rIndicatedRfbList, prQueEntry);
    }

    KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_RX_QUE);
    return;
} /* end of nicRxReturnRFB() */

/*----------------------------------------------------------------------------*/
/*!
* @brief Process rx interrupt. When the rx
*        Interrupt is asserted, it means there are frames in queue.
*
* @param prAdapter      Pointer to the Adapter structure.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
nicProcessRxInterrupt (
    IN  P_ADAPTER_T prAdapter
    )
{
    ASSERT(prAdapter);

#if CFG_SDIO_INTR_ENHANCE
    #if CFG_SDIO_RX_AGG
        nicRxSDIOAggReceiveRFBs(prAdapter);
    #else
        nicRxSDIOReceiveRFBs(prAdapter);
    #endif
#else
    nicRxReceiveRFBs(prAdapter);
#endif /* CFG_SDIO_INTR_ENHANCE */

    nicRxProcessRFBs(prAdapter);

    return;

} /* end of nicProcessRxInterrupt() */


#if CFG_TCP_IP_CHKSUM_OFFLOAD
/*----------------------------------------------------------------------------*/
/*!
* @brief Used to update IP/TCP/UDP checksum statistics of RX Module.
*
* @param prAdapter  Pointer to the Adapter structure.
* @param aeCSUM     The array of checksum result.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
nicRxUpdateCSUMStatistics (
    IN P_ADAPTER_T prAdapter,
    IN const ENUM_CSUM_RESULT_T aeCSUM[]
    )
{
    P_RX_CTRL_T prRxCtrl;

    ASSERT(prAdapter);
    ASSERT(aeCSUM);

    prRxCtrl = &prAdapter->rRxCtrl;
    ASSERT(prRxCtrl);

    if ((aeCSUM[CSUM_TYPE_IPV4] == CSUM_RES_SUCCESS) ||
        (aeCSUM[CSUM_TYPE_IPV6] == CSUM_RES_SUCCESS)) {

        RX_INC_CNT(prRxCtrl, RX_CSUM_IP_SUCCESS_COUNT);
    }
    else if ((aeCSUM[CSUM_TYPE_IPV4] == CSUM_RES_FAILED) ||
             (aeCSUM[CSUM_TYPE_IPV6] == CSUM_RES_FAILED)) {

        RX_INC_CNT(prRxCtrl, RX_CSUM_IP_FAILED_COUNT);
    }
    else if ((aeCSUM[CSUM_TYPE_IPV4] == CSUM_RES_NONE) &&
             (aeCSUM[CSUM_TYPE_IPV6] == CSUM_RES_NONE)) {

        RX_INC_CNT(prRxCtrl, RX_CSUM_UNKNOWN_L3_PKT_COUNT);
    }
    else {
        ASSERT(0);
    }

    if (aeCSUM[CSUM_TYPE_TCP] == CSUM_RES_SUCCESS) {
        RX_INC_CNT(prRxCtrl, RX_CSUM_TCP_SUCCESS_COUNT);
    }
    else if (aeCSUM[CSUM_TYPE_TCP] == CSUM_RES_FAILED) {
        RX_INC_CNT(prRxCtrl, RX_CSUM_TCP_FAILED_COUNT);
    }
    else if (aeCSUM[CSUM_TYPE_UDP] == CSUM_RES_SUCCESS) {
        RX_INC_CNT(prRxCtrl, RX_CSUM_UDP_SUCCESS_COUNT);
    }
    else if (aeCSUM[CSUM_TYPE_UDP] == CSUM_RES_FAILED) {
        RX_INC_CNT(prRxCtrl, RX_CSUM_UDP_FAILED_COUNT);
    }
    else if ((aeCSUM[CSUM_TYPE_UDP] == CSUM_RES_NONE) &&
             (aeCSUM[CSUM_TYPE_TCP] == CSUM_RES_NONE)) {

        RX_INC_CNT(prRxCtrl, RX_CSUM_UNKNOWN_L4_PKT_COUNT);
    }
    else {
        ASSERT(0);
    }

    return;
} /* end of nicRxUpdateCSUMStatistics() */
#endif /* CFG_TCP_IP_CHKSUM_OFFLOAD */


/*----------------------------------------------------------------------------*/
/*!
* @brief This function is used to query current status of RX Module.
*
* @param prAdapter      Pointer to the Adapter structure.
* @param pucBuffer      Pointer to the message buffer.
* @param pu4Count      Pointer to the buffer of message length count.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
nicRxQueryStatus (
    IN P_ADAPTER_T prAdapter,
    IN PUINT_8 pucBuffer,
    OUT PUINT_32 pu4Count
    )
{
    P_RX_CTRL_T prRxCtrl;
    PUINT_8 pucCurrBuf = pucBuffer;


    ASSERT(prAdapter);
    prRxCtrl = &prAdapter->rRxCtrl;
    ASSERT(prRxCtrl);

    //if (pucBuffer) {} /* For Windows, we'll print directly instead of sprintf() */
    ASSERT(pu4Count);

    SPRINTF(pucCurrBuf, ("\n\nRX CTRL STATUS:"));
    SPRINTF(pucCurrBuf, ("\n==============="));
    SPRINTF(pucCurrBuf, ("\nFREE RFB w/i BUF LIST :%9u", prRxCtrl->rFreeSwRfbList.u4NumElem));
    SPRINTF(pucCurrBuf, ("\nFREE RFB w/o BUF LIST :%9u", prRxCtrl->rIndicatedRfbList.u4NumElem));
    SPRINTF(pucCurrBuf, ("\nRECEIVED RFB LIST     :%9u", prRxCtrl->rReceivedRfbList.u4NumElem));

    SPRINTF(pucCurrBuf, ("\n\n"));

// *pu4Count = (UINT_32)((UINT_32)pucCurrBuf - (UINT_32)pucBuffer);

    return;
} /* end of nicRxQueryStatus() */


/*----------------------------------------------------------------------------*/
/*!
* @brief Clear RX related counters
*
* @param prAdapter Pointer of Adapter Data Structure
*
* @return - (none)
*/
/*----------------------------------------------------------------------------*/
VOID
nicRxClearStatistics (
    IN P_ADAPTER_T prAdapter
    )
{
    P_RX_CTRL_T prRxCtrl;

    ASSERT(prAdapter);
    prRxCtrl = &prAdapter->rRxCtrl;
    ASSERT(prRxCtrl);

    RX_RESET_ALL_CNTS(prRxCtrl);
    return;
}


/*----------------------------------------------------------------------------*/
/*!
* @brief This function is used to query current statistics of RX Module.
*
* @param prAdapter      Pointer to the Adapter structure.
* @param pucBuffer      Pointer to the message buffer.
* @param pu4Count      Pointer to the buffer of message length count.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
nicRxQueryStatistics (
    IN P_ADAPTER_T prAdapter,
    IN PUINT_8 pucBuffer,
    OUT PUINT_32 pu4Count
    )
{
    P_RX_CTRL_T prRxCtrl;
    PUINT_8 pucCurrBuf = pucBuffer;

    ASSERT(prAdapter);
    prRxCtrl = &prAdapter->rRxCtrl;
    ASSERT(prRxCtrl);

    //if (pucBuffer) {} /* For Windows, we'll print directly instead of sprintf() */
    ASSERT(pu4Count);

#define SPRINTF_RX_COUNTER(eCounter) \
    SPRINTF(pucCurrBuf, ("%-30s : %u\n", #eCounter, (UINT_32)prRxCtrl->au8Statistics[eCounter]))

    SPRINTF_RX_COUNTER(RX_MPDU_TOTAL_COUNT);
    SPRINTF_RX_COUNTER(RX_SIZE_ERR_DROP_COUNT);
    SPRINTF_RX_COUNTER(RX_DATA_INDICATION_COUNT);
    SPRINTF_RX_COUNTER(RX_DATA_RETURNED_COUNT);
    SPRINTF_RX_COUNTER(RX_DATA_RETAINED_COUNT);

#if CFG_TCP_IP_CHKSUM_OFFLOAD || CFG_TCP_IP_CHKSUM_OFFLOAD_NDIS_60
    SPRINTF_RX_COUNTER(RX_CSUM_TCP_FAILED_COUNT);
    SPRINTF_RX_COUNTER(RX_CSUM_UDP_FAILED_COUNT);
    SPRINTF_RX_COUNTER(RX_CSUM_IP_FAILED_COUNT);
    SPRINTF_RX_COUNTER(RX_CSUM_TCP_SUCCESS_COUNT);
    SPRINTF_RX_COUNTER(RX_CSUM_UDP_SUCCESS_COUNT);
    SPRINTF_RX_COUNTER(RX_CSUM_IP_SUCCESS_COUNT);
    SPRINTF_RX_COUNTER(RX_CSUM_UNKNOWN_L4_PKT_COUNT);
    SPRINTF_RX_COUNTER(RX_CSUM_UNKNOWN_L3_PKT_COUNT);
    SPRINTF_RX_COUNTER(RX_IP_V6_PKT_CCOUNT);
#endif

// *pu4Count = (UINT_32)(pucCurrBuf - pucBuffer);

    nicRxClearStatistics(prAdapter);

    return;
}

/*----------------------------------------------------------------------------*/
/*!
* @brief Read the Response data from data port
*
* @param prAdapter pointer to the Adapter handler
* @param pucRspBuffer pointer to the Response buffer
*
* @retval WLAN_STATUS_SUCCESS: Response packet has been read
* @retval WLAN_STATUS_FAILURE: Read Response packet timeout or error occurred
*
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
nicRxWaitResponse (
    IN P_ADAPTER_T prAdapter,
    IN UINT_8 ucPortIdx,
    OUT PUINT_8 pucRspBuffer,
    IN UINT_32 u4MaxRespBufferLen,
    OUT PUINT_32 pu4Length
    )
{
    UINT_32 u4Value = 0, u4PktLen = 0;
	UINT_32 i = 0;
    WLAN_STATUS u4Status = WLAN_STATUS_SUCCESS;
    BOOLEAN fgResult = TRUE;
    UINT_32 u4Time, u4Current;

    DEBUGFUNC("nicRxWaitResponse");

    ASSERT(prAdapter);
    ASSERT(pucRspBuffer);
    ASSERT(ucPortIdx < 2);

    u4Time = kalGetTimeTick();

    do {
        /* Read the packet length */
        HAL_MCR_RD(prAdapter, MCR_WRPLR, &u4Value);

        if (!fgResult) {
            DBGLOG(RX, ERROR, ("Read Response Packet Error\n"));
            return WLAN_STATUS_FAILURE;
        }

        if(ucPortIdx == 0) {
            u4PktLen = u4Value & 0xFFFF;
        }
        else {
            u4PktLen = (u4Value >> 16) & 0xFFFF;
        }

//        DBGLOG(RX, TRACE, ("i = %d, u4PktLen = %d\n", i, u4PktLen));

        if (u4PktLen == 0) {
            /* timeout exceeding check */
            u4Current = kalGetTimeTick();

            if((u4Current > u4Time) && ((u4Current - u4Time) > RX_RESPONSE_TIMEOUT)) {
				DBGLOG(RX, ERROR,("RX_RESPONSE_TIMEOUT1 %u %d %u\n", u4PktLen, i,
					u4Current));
                return WLAN_STATUS_FAILURE;
            }
            else if(u4Current < u4Time && ((u4Current + (0xFFFFFFFF - u4Time)) > RX_RESPONSE_TIMEOUT)) {
				DBGLOG(RX, ERROR,("RX_RESPONSE_TIMEOUT2 %u %d %u\n", u4PktLen, i, u4Current));
                return WLAN_STATUS_FAILURE;
            }

            /* Response packet is not ready */
            kalUdelay(50);

            i++;
        }
        else if (u4PktLen > u4MaxRespBufferLen) {
            /*
                    TO: buffer is not enough but we still need to read all data from HIF to avoid
                    HIF crazy.
                */
            DBGLOG(RX, ERROR, ("Not enough Event Buffer: required length = 0x%x, available buffer length = %d\n",
                u4PktLen, u4MaxRespBufferLen));
			DBGLOG(RX, ERROR, ("i = %d, u4PktLen = %u\n", i, u4PktLen));
            return WLAN_STATUS_FAILURE;
        }
        else {
            HAL_PORT_RD(prAdapter,
                        ucPortIdx == 0 ? MCR_WRDR0 : MCR_WRDR1,
                        u4PktLen,
                        pucRspBuffer,
                        u4MaxRespBufferLen);

            /* fgResult will be updated in MACRO */
            if (!fgResult) {
                DBGLOG(RX, ERROR, ("Read Response Packet Error\n"));
                return WLAN_STATUS_FAILURE;
            }

            DBGLOG(RX, TRACE, ("Dump Response buffer, length = 0x%x\n",
                u4PktLen));
            DBGLOG_MEM8(RX, TRACE, pucRspBuffer, u4PktLen);

            *pu4Length = u4PktLen;
            break;
        }
    } while(TRUE);

    return u4Status;
}

/*----------------------------------------------------------------------------*/
/*!
* @brief Set filter to enable Promiscuous Mode
*
* @param prAdapter          Pointer to the Adapter structure.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
nicRxEnablePromiscuousMode (
    IN P_ADAPTER_T prAdapter
    )
{
    ASSERT(prAdapter);

    return;
} /* end of nicRxEnablePromiscuousMode() */


/*----------------------------------------------------------------------------*/
/*!
* @brief Set filter to disable Promiscuous Mode
*
* @param prAdapter  Pointer to the Adapter structure.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
nicRxDisablePromiscuousMode (
    IN P_ADAPTER_T prAdapter
    )
{
    ASSERT(prAdapter);

    return;
} /* end of nicRxDisablePromiscuousMode() */


/*----------------------------------------------------------------------------*/
/*!
* @brief this function flushes all packets queued in reordering module
*
* @param prAdapter              Pointer to the Adapter structure.
*
* @retval WLAN_STATUS_SUCCESS   Flushed successfully
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
nicRxFlush (
    IN P_ADAPTER_T  prAdapter
    )
{
    P_SW_RFB_T prSwRfb;

    ASSERT(prAdapter);

    if((prSwRfb = qmFlushRxQueues(prAdapter)) != NULL) {
        do {
            P_SW_RFB_T prNextSwRfb;

            // save next first
            prNextSwRfb = (P_SW_RFB_T)QUEUE_GET_NEXT_ENTRY((P_QUE_ENTRY_T)prSwRfb);

            // free
            nicRxReturnRFB(prAdapter, prSwRfb);

            prSwRfb = prNextSwRfb;
        } while(prSwRfb);
    }

    return WLAN_STATUS_SUCCESS;
}


/*----------------------------------------------------------------------------*/
/*!
* @brief
*
* @param
*
* @retval
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
nicRxProcessActionFrame (
    IN P_ADAPTER_T      prAdapter,
    IN P_SW_RFB_T       prSwRfb
    )
{
    P_WLAN_ACTION_FRAME prActFrame;

    ASSERT(prAdapter);
    ASSERT(prSwRfb);

    if (prSwRfb->u2PacketLen < sizeof(WLAN_ACTION_FRAME) - 1) {
        return WLAN_STATUS_INVALID_PACKET;
    }
    prActFrame = (P_WLAN_ACTION_FRAME) prSwRfb->pvHeader;
    DBGLOG(RX, INFO,("Category %u\n", prActFrame->ucCategory));

    switch (prActFrame->ucCategory) {
    case CATEGORY_PUBLIC_ACTION:
        if (HIF_RX_HDR_GET_NETWORK_IDX(prSwRfb->prHifRxHdr) == NETWORK_TYPE_AIS_INDEX) {
            aisFuncValidateRxActionFrame(prAdapter, prSwRfb);
        }
    #if CFG_ENABLE_WIFI_DIRECT
        else if (prAdapter->fgIsP2PRegistered) {
            rlmProcessPublicAction(prAdapter, prSwRfb);

            p2pFuncValidateRxActionFrame(
                        prAdapter, prSwRfb);

        }
    #endif
        break;

    case CATEGORY_HT_ACTION:
    #if CFG_ENABLE_WIFI_DIRECT
        if (prAdapter->fgIsP2PRegistered) {
            rlmProcessHtAction(prAdapter, prSwRfb);
        }
    #endif
        break;
    case CATEGORY_VENDOR_SPECIFIC_ACTION:
    #if CFG_ENABLE_WIFI_DIRECT
        if (prAdapter->fgIsP2PRegistered) {
            p2pFuncValidateRxActionFrame(prAdapter, prSwRfb);
        }
    #endif
        break;
#if CFG_SUPPORT_802_11W
    case CATEGORY_SA_QUERT_ACTION:
        {
            P_HIF_RX_HEADER_T   prHifRxHdr;

            prHifRxHdr = prSwRfb->prHifRxHdr;

            if ((HIF_RX_HDR_GET_NETWORK_IDX(prHifRxHdr) == NETWORK_TYPE_AIS_INDEX) &&
                prAdapter->rWifiVar.rAisSpecificBssInfo.fgMgmtProtection /* Use MFP */
                ) {
                if (!(prHifRxHdr->ucReserved & CONTROL_FLAG_UC_MGMT_NO_ENC)) {
                    /* MFP test plan 5.3.3.4 */
                    rsnSaQueryAction(prAdapter, prSwRfb);
                }
                else {
                    DBGLOG(RSN, TRACE, ("Un-Protected SA Query, do nothing\n"));
                }
            }
        }
        break;
#endif
#if CFG_SUPPORT_802_11V
    case CATEGORY_WNM_ACTION:
        {
            wnmWNMAction(prAdapter, prSwRfb);
        }
        break;
#endif

#if CFG_SUPPORT_DFS // Add by Enlai
case CATEGORY_SPEC_MGT:
    {
        if(prAdapter->fgEnable5GBand == TRUE)
           rlmProcessSpecMgtAction(prAdapter, prSwRfb);
    }
    break;
#endif

#if (CFG_SUPPORT_TDLS == 1)
		case 12: /* shall not be here */
			/*
			 	A received TDLS Action frame with the Type field set to Management shall
				be discarded. Note that the TDLS Discovery Response frame is not a TDLS
				frame but a Public Action frame.
			*/
			break;
#endif /* CFG_SUPPORT_TDLS */

    default:
        break;
    } /* end of switch case */

    return WLAN_STATUS_SUCCESS;
}


