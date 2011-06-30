/*
 * Copyright (C) 2010 ST-Ericsson AB
 * License terms: GNU General Public License (GPL) version 2
 * TCM memory test
 *
 * Author: Linus Walleij <linus.walleij@stericsson.com>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <asm/bug.h>
#include <asm/tcm.h>

#define CANARY1 0xDEADBEEFU
#define CANARY2 0x2BADBABEU
#define CANARY3 0xCAFEBABEU

/* Uninitialized data */
static u32 __tcmdata tcmvar;
/* Initialized data */
static u32 __tcmdata tcmassigned = CANARY2;
/* Constant */
static const u32 __tcmconst tcmconst = CANARY3;

static void __tcmlocalfunc tcm_to_tcm(void)
{
	int i;
	for (i = 0; i < 100; i++)
		tcmvar ++;
}

static void __tcmfunc hello_tcm(void)
{
	/* Some abstract code that runs in ITCM */
	int i;
	for (i = 0; i < 100; i++) {
		tcmvar ++;
	}
	tcm_to_tcm();
}

static int __init test_tcm(void)
{
	u32 *tcmem;
	int i;

	if (!tcm_dtcm_present() && !tcm_itcm_present()) {
		pr_info("CPU: no TCMs present, skipping tests\n");
		return 0;
	}

	if (!tcm_itcm_present())
		goto skip_itcm_tests;

	hello_tcm();
	pr_info("CPU: hello TCM executed from ITCM RAM\n");

	pr_info("CPU: TCM variable from testrun: %u @ %p\n",
		tcmvar, &tcmvar);
	BUG_ON(tcmvar != 200);

skip_itcm_tests:
	tcmvar = CANARY1;
	pr_info("CPU: TCM variable: 0x%x @ %p\n", tcmvar, &tcmvar);
	BUG_ON(tcmvar != CANARY1);

	pr_info("CPU: TCM assigned variable: 0x%x @ %p\n",
		tcmassigned, &tcmassigned);
	BUG_ON(tcmassigned != CANARY2);

	pr_info("CPU: TCM constant: 0x%x @ %p\n", tcmconst, &tcmconst);
	BUG_ON(tcmconst != CANARY3);

	/* Allocate some TCM memory from the pool */
	tcmem = tcm_alloc(20);
	if (tcmem) {
		pr_info("CPU: TCM Allocated 20 bytes of TCM @ %p\n", tcmem);
		tcmem[0] = CANARY1;
		tcmem[1] = CANARY2;
		tcmem[2] = CANARY3;
		tcmem[3] = CANARY1;
		tcmem[4] = CANARY2;
		for (i = 0; i < 5; i++)
			pr_info("CPU: TCM tcmem[%d] = %08x\n", i, tcmem[i]);
		BUG_ON(tcmem[0] != CANARY1);
		BUG_ON(tcmem[1] != CANARY2);
		BUG_ON(tcmem[2] != CANARY3);
		BUG_ON(tcmem[3] != CANARY1);
		BUG_ON(tcmem[4] != CANARY2);
		tcm_free(tcmem, 20);
	}
	return 0;
}

late_initcall(test_tcm);
