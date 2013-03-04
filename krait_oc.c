/*
 * Overclocking driver for Qualcomm MSM8960 (Krait) devices, such as the
 * HTC One XL (AT&T/Rogers/Asia), HTC One S HTC One VX. This method is recommended for
 * rooted devices without kernel source or a locked bootloader.
 *
 * Successfully compiled against CMs msm8960 kernel source,
 * Must completely compile kernel in source first.  Doesn't matter which defconfig
 *
 * Please encourage HTC and AT&T to allow the AT&T HTC One X's bootloader to be
 * unlocked! Even if it doesn't affect you, and even if it gets S-OFF
 * eventually, this sets a bad precedent for carriers to get in the way of
 * manufacturers' intentions of device freedom.
 *
 * Copyright (c) 2012 Michael Huang
 * Author: Michael Huang <mike@setcpu.com>
 *
 * Modified by: Miguel Boton <mboton@gmail.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 */

#include <linux/cpufreq.h>
#include <linux/kallsyms.h>

#define DRIVER_AUTHOR "Michael Huang <mike@setcpu.com>, Miguel Boton <mboton@gmail.com>"
#define DRIVER_DESCRIPTION "MSM 8960 Overclock Driver"
#define DRIVER_VERSION "1.1"

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESCRIPTION);
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");

/* Speed of the HFPLL in KHz */
#define HFPLL_FREQ_KHZ 27000

/* Name of the acpu_freq_tbl symbols */
#define ACPU_FREQ_TBL_NOM_NAME "acpu_freq_tbl_8960_kraitv2_nom"
#define ACPU_FREQ_TBL_SLOW_NAME "acpu_freq_tbl_8960_kraitv2_slow"
#define ACPU_FREQ_TBL_FAST_NAME "acpu_freq_tbl_8960_kraitv2_fast"

/* Module parameters */
/* PLL L value. Controls overclocked frequency. 27 MHz * pll_l_val = X MHz */
static uint pll_l_val = 0x47;

/* Voltage of the overclocked frequency, in uV. Max supported is 1300000 uV */
static uint vdd_uv = 1300000;

module_param(pll_l_val, uint, 0444);
module_param(vdd_uv, uint, 0444);
MODULE_PARM_DESC(pll_l_val, "Frequency multiplier for overclocked frequency");
MODULE_PARM_DESC(vdd_uv, "Core voltage in uV for overclocked frequency");

/* New frequency table */
static struct cpufreq_frequency_table freq_table[] = {
	{ 0, 384000 },
	{ 1, 486000 },
	{ 2, 594000 },
	{ 3, 702000 },
	{ 4, 810000 },
	{ 5, 918000 },
	{ 6, 1026000 },
	{ 7, 1134000 },
	{ 8, 1242000 },
	{ 9, 1350000 },
	{ 10, 1458000 },
	{ 11, 1512000 },
	/* We replace this row with our desired frequency later */
	{ 12, 1620000 },
	{ 13, 1728000 },
	{ 14, 1809000 },
	{ 15, 1917000 },
	{ 16, CPUFREQ_TABLE_END },
};

#define FREQ_TABLE_SIZE (sizeof(freq_table) / sizeof(struct cpufreq_frequency_table))
#define FREQ_TABLE_START 12
#define FREQ_TABLE_LAST (FREQ_TABLE_SIZE - 2)

struct core_speed {
	unsigned int		khz;
	int			src;
	unsigned int		pri_src_sel;
	unsigned int		sec_src_sel;
	unsigned int		pll_l_val;
};

struct acpu_level {
	unsigned int		use_for_scaling;
	struct core_speed	speed;
	struct l2_level		*l2_level;
	unsigned int		vdd_core;
};

struct freq_voltage {
	unsigned int khz;
	unsigned int vdd;
};

/* Frequency-Voltage table */
static struct freq_voltage freq_vdd_table[] = {
	{ 1620000, 1200000 },
	{ 1728000, 1200000 },
	{ 1809000, 1250000 },
	{ 1917000, 1300000 },
	{ 0, 0 },
};

struct cpufreq_frequency_table *orig_table;

/* Use a function pointer for cpufreq_cpu_get because the symbol version
 * differs in the HTC kernel and the Code Aurora kernel, so the kernel won't
 * let us call it normally.
 *
 * Call the function normally when the kernel source is released.
 */
typedef struct cpufreq_policy *(*cpufreq_cpu_get_type)(int); 
struct cpufreq_policy *(*cpufreq_cpu_get_new)(int);

/* Updates a row in a struct acpu_level with symbol name symbol_name */
static void __init acpu_freq_row_update(char *symbol_name)
{
	struct acpu_level *acpu_freq_tbl;
	ulong acpu_freq_tbl_addr;
	uint i, index;

	acpu_freq_tbl_addr = kallsyms_lookup_name(symbol_name);

	if(acpu_freq_tbl_addr == 0) {
		printk(KERN_WARNING "krait_oc: symbol not found\n");
		printk(KERN_WARNING "krait_oc: skipping this table\n");
		return;
	}

	acpu_freq_tbl = (struct acpu_level*) acpu_freq_tbl_addr;

	for (i = FREQ_TABLE_START, index = 1; freq_table[i].frequency != CPUFREQ_TABLE_END; i++, index++) {
		unsigned int vdd = vdd_uv;
		uint j;

		/* Find a valid entry */
		while (acpu_freq_tbl[index].use_for_scaling)
			index++;

		/* Find voltage value */
		for (j = 0; freq_vdd_table[j].khz; j++) {
			if (freq_vdd_table[j].khz == freq_table[i].frequency) {
				vdd = freq_vdd_table[j].vdd;
				break;
			}
		}

		acpu_freq_tbl[index].speed.khz = freq_table[i].frequency;
		acpu_freq_tbl[index].speed.pll_l_val = freq_table[i].frequency / HFPLL_FREQ_KHZ;
		acpu_freq_tbl[index].vdd_core = vdd;

		printk(KERN_WARNING "krait_oc: [%d] KHz=%d PLL=%d VDD=%d\n", index, freq_table[i].frequency, freq_table[i].frequency / HFPLL_FREQ_KHZ, vdd);
	}
}

static int __init overclock_init(void)
{
	struct cpufreq_policy *policy;
	ulong cpufreq_cpu_get_addr;
	uint cpu;

	printk(KERN_INFO "krait_oc: %s version %s\n", DRIVER_DESCRIPTION,
		DRIVER_VERSION);
	printk(KERN_INFO "krait_oc: by %s\n", DRIVER_AUTHOR);
	printk(KERN_INFO "krait_oc: overclocking to %u at %u uV\n",
		pll_l_val*HFPLL_FREQ_KHZ, vdd_uv);

	printk(KERN_INFO "krait_oc: updating cpufreq policy\n");

	cpufreq_cpu_get_addr = kallsyms_lookup_name("cpufreq_cpu_get");

	if(cpufreq_cpu_get_addr == 0) {
		printk(KERN_WARNING "krait_oc: symbol not found\n");
		printk(KERN_WARNING "krait_oc: not attempting overclock\n");
		return 0;
	}

	cpufreq_cpu_get_new = (cpufreq_cpu_get_type) cpufreq_cpu_get_addr;

	policy = cpufreq_cpu_get_new(0);
	policy->cpuinfo.max_freq = pll_l_val*HFPLL_FREQ_KHZ;

	printk(KERN_INFO "krait_oc: updating cpufreq tables\n");
	freq_table[FREQ_TABLE_LAST].frequency = pll_l_val*HFPLL_FREQ_KHZ;

	/* Save a pointer to the freq original table to restore if unloaded */
	orig_table = cpufreq_frequency_get_table(0);

	for_each_possible_cpu(cpu) {
		cpufreq_frequency_table_put_attr(cpu);
		cpufreq_frequency_table_get_attr(freq_table, cpu);
	}

	/* Index 20 is not used for scaling in the acpu_freq_tbl, so fill it
         * with our new freq. Change all three tables to account for all
	 * possible bins. */
	printk(KERN_INFO "krait_oc: updating nominal acpu_freq_tbl\n");
	acpu_freq_row_update(ACPU_FREQ_TBL_NOM_NAME);
	printk(KERN_INFO "krait_oc: updating slow acpu_freq_tbl\n");
	acpu_freq_row_update(ACPU_FREQ_TBL_SLOW_NAME);
	printk(KERN_INFO "krait_oc: updating fast acpu_freq_tbl\n");
	acpu_freq_row_update(ACPU_FREQ_TBL_FAST_NAME);

	return 0;
}

static void __exit overclock_exit(void)
{
	struct cpufreq_policy *policy;
	uint cpu;

	if(kallsyms_lookup_name("cpufreq_cpu_get") != 0) {
		printk(KERN_INFO "krait_oc: reverting cpufreq policy\n");
		policy = cpufreq_cpu_get_new(0);
		policy->cpuinfo.max_freq = 1512000;

		printk(KERN_INFO "krait_oc: reverting cpufreq tables\n");
		for_each_possible_cpu(cpu) {
			cpufreq_frequency_table_put_attr(cpu);
			cpufreq_frequency_table_get_attr(orig_table, cpu);
		}
	}

	printk(KERN_INFO "krait_oc: unloaded\n");
}

module_init(overclock_init);
module_exit(overclock_exit);
