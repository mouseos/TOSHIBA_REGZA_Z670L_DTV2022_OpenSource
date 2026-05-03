#ifndef MACH_PROFILE_H
#define MACH_PROFILE_H

#define USE_WD_PROFILE

#ifdef USE_WD_PROFILE

extern u64 wd_counter[];
extern volatile u32 wd_start[], wd_stop[];
extern u32 wd_on[];
extern u32 wd_freq;

#define INIT_COUNT	\
	u64 wd_counter[32]; \
	volatile u32 wd_start[32], wd_stop[32]; \
	u32 wd_on[32]; \
	u32 wd_freq;

#define START_COUNT(index)    ({ \
		BUG_ON(wd_on[index]==1); \
		wd_on[index]=1; \
		wd_start[index]=*(volatile u32 *)(XC_SOC_PROC_MMREG_BASE+PROC5_WATCHDOG_CNT); \
		})

#define STOP_COUNT(index)       ({ \
		BUG_ON(wd_on[index]==0); \
        wd_stop[index]=*(volatile u32 *)(XC_SOC_PROC_MMREG_BASE+PROC5_WATCHDOG_CNT); \
        if(wd_stop[index]>=wd_start[index]) \
            wd_counter[index]+=(u64)(wd_stop[index]-wd_start[index]); \
        else \
            wd_counter[index]+= (u64)((0xffffffff-wd_start[index]+1)+wd_stop[index]); \
        wd_start[index]=0; \
		wd_on[index]=0; \
    })

#define GET_COUNT(index) (wd_counter[index])
#define GET_COUNT_IN_MSEC(index) ({ \
			u64 res=wd_counter[index]; \
			u32 rem; \
			res*=1000; \
			rem=do_div(res, wd_freq); \
			res; \
			})

#define CALIBRATE_COUNT \
	memset(wd_counter, 0, sizeof(wd_counter)); \
	memset(wd_on, 0, sizeof(wd_on)); \
	START_COUNT(0); \
	mdelay(1000); \
	STOP_COUNT(0); \
	wd_freq=wd_counter[0]; \
	CLEAR_COUNT(0); \
	printk(">>>>>>>>>>>>>>>>>>>>>>>>>>>> Watchdog run at %dMHz\n", wd_freq/1000000);
	
#define CLEAR_COUNT(index) ({ \
			wd_counter[index]=0; \
			wd_on[index]=0; \
			})

#else

#define INIT_COUNT
#define START_COUNT(index)
#define STOP_COUNT(index)
#define CALIBRATE_COUNT

#endif //USE_WD_PROFILE

#endif //MACH_PROFILE_H
