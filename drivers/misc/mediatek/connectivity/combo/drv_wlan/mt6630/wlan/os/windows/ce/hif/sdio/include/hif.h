/*
** $Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_3/os/windows/ce/hif/sdio/include/hif.h#1 $
*/

/*! \file   "hif.h"
    \brief  sdio specific structure for GLUE layer

    N/A
*/





#ifndef _HIF_H
#define _HIF_H
/******************************************************************************
*                         C O M P I L E R   F L A G S
*******************************************************************************
*/

/******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
*******************************************************************************
*/
#include "SDCardDDK.h"
#if CFG_SDIO_PATHRU_MODE
#include <sdhcd.h>		/* definition of PSDCARD_HC_CONTEXT */
#endif

#if _PF_COLIBRI
#include "colibri.h"		/* definition of platformBusInit() */
#endif
#if _PF_MT6516
#include "mt6516.h"		/* definition of platformBusInit() */
#endif

#ifdef MT6620
#define SDNDIS_REG_PATH TEXT("\\Comm\\MT6620")
#endif

#include "bldver.h"

#define BLOCK_TRANSFER_LEN (512)

#ifdef __cplusplus
extern "C" {
	/* NDIS ddk header */
#include <ndis.h>
}
#endif
#if (CE_MAJOR_VER >= 4)
#define NDIS_SUCCESS(Status) ((NDIS_STATUS)(Status) == NDIS_STATUS_SUCCESS)
#endif
/******************************************************************************
*                              C O N S T A N T S
*******************************************************************************
*/
#define MAX_ACTIVE_REG_PATH         256
#define NIC_INTERFACE_TYPE          NdisInterfaceInternal
#define NIC_ATTRIBUTE               NDIS_ATTRIBUTE_DESERIALIZE
#define NIC_DMA_MAPPED              0
#define NIC_MINIPORT_INT_REG        0
#ifdef X86_CPU
/* Please make sure the MCR you wrote will not take any effect.
 * MCR_MIBSDR (0x00C4) has confirm with DE.
 */* / TODO: yarco */
#define SDIO_X86_WORKAROUND_WRITE_MCR  0x0000
#endif
#if CFG_SDIO_PATHRU_MODE
#define SDIO_PATHRU_SHC_NAME    TEXT("SHC1:")
#define FILE_DEVICE_SDHC    (0x8CE7)	/* MTK custom file device for SDHC */
#define _SDHC_CTL_CODE(_Function, _Method, _Access)  \
    CTL_CODE(FILE_DEVICE_SDHC, _Function, _Method, _Access)
#define IOCTL_SDHC_PATHRU _SDHC_CTL_CODE(0, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif
/******************************************************************************
*                             D A T A   T Y P E S
*******************************************************************************
*/
#if CFG_SDIO_PATHRU_MODE
/* SDIO PATHRU mode's data structure, which is passed into SDHC driver and is
** attached to HIF info structure.
 */ typedef struct _GL_PATHRU_INFO_IN_T {
	DWORD dwEnable;		/* To enable or disable PATHRU mode */
	DWORD dwSlotNumber;	/* Target slot number to be controlled */
	 VOID(*pIndicateSlotStateChange) (PSDCARD_HC_CONTEXT pHCContext,	/* Status change event handler in PATHRU */
					  DWORD SlotNumber, SD_SLOT_EVENT Event, PVOID pvClient);
	PVOID pvClientContext;	/* Client's context used for status indication */
} GL_PATHRU_INFO_IN_T, *P_GL_PATHRU_INFO_IN_T;

/* SDIO PATHRU mode's data structure, which is outputted by SDHC driver and is
** attached to HIF info structure.
 */
typedef struct _GL_PATHRU_INFO_OUT_T {
	PSDCARD_HC_CONTEXT pHcd;	/* Context of SDHC driver , which is returned by SDHC driver */
	 VOID(*pIndicateSlotStateChange) (PSDCARD_HC_CONTEXT pHCContext,	/* Status change event handler in SDHC */
					  DWORD SlotNumber, SD_SLOT_EVENT Event);
} GL_PATHRU_INFO_OUT_T, *P_GL_PATHRU_INFO_OUT_T;

/* SDIO PATHRU mode's data structure, which is attached to HIF info structure.
 */
typedef struct _GL_PATHRU_INFO_T {
	HANDLE hSHCDev;		/* handle to SHC */
	TCHAR szSHCDevName[16];	/* SHC name */
	BOOLEAN fgInitialized;	/* pass-through mode initialized or not */
	BOOLEAN fgEnabled;	/* pass-through mode enabled or not */
	DWORD dwSlotNumber;	/* slot number passed to SDHC APIs */
	PSDCARD_HC_CONTEXT pSHCContext;	/* Context of SDHC driver , which is copied from rInfoOut */
	GL_PATHRU_INFO_IN_T rInfoIn;	/* Info to be passed into SDHC driver */
	GL_PATHRU_INFO_OUT_T rInfoOut;	/* Info returned by SDHC driver */
	CRITICAL_SECTION rLock;	/* CriticalSetction for pretecting PATHRU atomic operation and integrity */
} GL_PATHRU_INFO_T, *P_GL_PATHRU_INFO_T;
#endif				/* CFG_SDIO_PATHRU_MODE */

/* host interface's private data structure, which is attached to os glue
** layer info structure.
 */
typedef struct _GL_HIF_INFO_T {
	SD_DEVICE_HANDLE hDevice;	/* handle to card */
	P_GLUE_INFO_T prGlueInfo;	/* handle to glue Info */
	UCHAR Function;		/* I/O function number */
	PWSTR pRegPath;		/* reg path for driver */
	ULONG Errors;		/* error count */
	SD_CARD_RCA RCA;	/* relative card address */
	SD_HOST_BLOCK_CAPABILITY sdHostBlockCap;
	ULONG WBlkBitSize;
	WCHAR ActivePath[MAX_ACTIVE_REG_PATH];

#if CFG_SDIO_PATHRU_MODE
	BOOLEAN fgSDIOFastPathEnable;	/* Fast-path feature in host and bus driver enabled or not */
	GL_PATHRU_INFO_T rPathruInfo;	/* Pass-through(PATHRU) info structure */
#endif

} GL_HIF_INFO_T, *P_GL_HIF_INFO_T;


/******************************************************************************
*                            P U B L I C   D A T A
*******************************************************************************
*/

/******************************************************************************
*                           P R I V A T E   D A T A
*******************************************************************************
*/

/******************************************************************************
*                                 M A C R O S
*******************************************************************************
*/
#ifdef CFG_HAVE_PLATFORM_INIT
#define sdioBusDeinit(prGlueInfo) \
    platformBusDeinit(prGlueInfo)
#define sdioSetPowerState(prGlueInfo, ePowerMode) \
    platformSetPowerState(prGlueInfo, ePowerMode)
#else
/* define platformBusInit() for platforms that have no such function. This is
** needed by sdio.c.
*/
#define platformBusInit(prGlueInfo) ((BOOLEAN)TRUE)
#define sdioBusDeinit(prGlueInfo)
#define sdioSetPowerState(prGlueInfo, ePowerMode)
#endif

/******************************************************************************
*                   F U N C T I O N   D E C L A R A T I O N S
*******************************************************************************
*/

#if CFG_SDIO_PATHRU_MODE
VOID sdioInitPathruMode(IN P_GLUE_INFO_T prGlueInfo);

VOID sdioDeinitPathruMode(IN P_GLUE_INFO_T prGlueInfo);

BOOLEAN sdioEnablePathruMode(IN P_GLUE_INFO_T prGlueInfo, IN BOOLEAN fgEnable);

#endif

#endif				/* _HIF_H */
