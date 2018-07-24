#ifndef __MMDVFS_MGR_H__
#define __MMDVFS_MGR_H__

#include <linux/aee.h>
#include <mt_smi.h>

#define MMDVFS_LOG_TAG	"MMDVFS"

#define MMDVFSMSG(string, args...) if(1){\
 pr_warn("[pid=%d]"string, current->tgid, ##args); \
 }
#define MMDVFSMSG2(string, args...) pr_warn(string, ##args)
#define MMDVFSTMP(string, args...) pr_warn("[pid=%d]"string, current->tgid, ##args)
#define MMDVFSERR(string, args...) do{\
	pr_err("error: "string, ##args); \
	aee_kernel_warning(MMDVFS_LOG_TAG, "error: "string, ##args);  \
} while(0)

#define MMSYS_CLK_LOW (0)
#define MMSYS_CLK_HIGH (1)
#define MMSYS_CLK_MEDIUM (2)

#define _BIT_(_bit_)                        (unsigned)(1 << (_bit_))
#define _BITS_(_bits_, _val_)               ((((unsigned) -1 >> (31 - ((1) ? _bits_))) & ~((1U << ((0) ? _bits_)) - 1)) & ((_val_)<<((0) ? _bits_)))
#define _BITMASK_(_bits_)                   (((unsigned) -1 >> (31 - ((1) ? _bits_))) & ~((1U << ((0) ? _bits_)) - 1))
#define _GET_BITS_VAL_(_bits_, _val_)       (((_val_) & (_BITMASK_(_bits_))) >> ((0) ? _bits_))


typedef int (*clk_switch_cb)(int ori_mmsys_clk_mode, int update_mmsys_clk_mode);
typedef int (*vdec_ctrl_cb)(void);

/* MMDVFS extern APIs */
extern void mmdvfs_init(MTK_SMI_BWC_MM_INFO *info);
extern void mmdvfs_handle_cmd(MTK_MMDVFS_CMD *cmd);
extern void mmdvfs_notify_scenario_enter(MTK_SMI_BWC_SCEN scen);
extern void mmdvfs_notify_scenario_exit(MTK_SMI_BWC_SCEN scen);
extern void mmdvfs_notify_scenario_concurrency(unsigned int u4Concurrency);
extern int register_mmclk_switch_cb(clk_switch_cb notify_cb,
clk_switch_cb notify_cb_nolock);
extern int mmdvfs_set_mmsys_clk(MTK_SMI_BWC_SCEN scenario, int mmsys_clk_mode);
extern void mt_set_vencpll_con1(int val);
extern int clkmux_sel(int id, unsigned int clksrc, char *name);

extern u32 get_devinfo_with_index(u32 index);

#define MMDVFS_PROFILE_UNKNOWN (0)
#define MMDVFS_PROFILE_R1 (1)
#define MMDVFS_PROFILE_J1 (2)
#define MMDVFS_PROFILE_D1 (3)
#define MMDVFS_PROFILE_D1_PLUS (4)
#define MMDVFS_PROFILE_D2 (5)
#define MMDVFS_PROFILE_D2_M_PLUS (6)
#define MMDVFS_PROFILE_D2_P_PLUS (7)
#define MMDVFS_PROFILE_D3 (8)
#define MMDVFS_PROFILE_E1 (9)

enum {
	MMDVFS_CAM_MON_SCEN = SMI_BWC_SCEN_CNT, MMDVFS_SCEN_MHL, MMDVFS_SCEN_MJC, MMDVFS_SCEN_DISP,
	MMDVFS_SCEN_ISP,	MMDVFS_SCEN_VP_HIGH_RESOLUTION , MMDVFS_SCEN_COUNT
};

extern int mmdvfs_get_mmdvfs_profile(void);
extern int is_mmdvfs_supported(void);

#endif /* __MMDVFS_MGR_H__ */

