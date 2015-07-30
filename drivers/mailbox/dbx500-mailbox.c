/*
 * Copyright (C) ST-Ericsson SA 2012
 *
 * License Terms: GNU General Public License v2
 * Author: Loic Pallardy <loic.pallardy@st.com> for ST-Ericsson
 * DBX500 PRCM Mailbox driver
 *
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mailbox_controller.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/semaphore.h>

// #include "mailbox.h"

#define MAILBOX_LEGACY		0
#define MAILBOX_UPAP		1

/* CPU mailbox registers */
#define PRCM_MBOX_CPU_VAL	0x0fc
#define PRCM_MBOX_CPU_SET	0x100
#define PRCM_MBOX_CPU_CLR	0x104

#define PRCM_ARM_IT1_CLR	0x48C
#define PRCM_ARM_IT1_VAL	0x494

#define NUM_MB 8
#define MBOX_BIT BIT
#define ALL_MBOX_BITS (MBOX_BIT(NUM_MB) - 1)

/* CPU mailbox share memory */
#define _PRCM_MBOX_HEADER		0x214 /* 16 bytes */
#define PRCM_MBOX_HEADER_REQ_MB0	(_PRCM_MBOX_HEADER + 0x0)
#define PRCM_MBOX_HEADER_REQ_MB1	(_PRCM_MBOX_HEADER + 0x1)
#define PRCM_MBOX_HEADER_REQ_MB2	(_PRCM_MBOX_HEADER + 0x2)
#define PRCM_MBOX_HEADER_REQ_MB3	(_PRCM_MBOX_HEADER + 0x3)
#define PRCM_MBOX_HEADER_REQ_MB4	(_PRCM_MBOX_HEADER + 0x4)
#define PRCM_MBOX_HEADER_REQ_MB5	(_PRCM_MBOX_HEADER + 0x5)
#define PRCM_MBOX_HEADER_ACK_MB0	(_PRCM_MBOX_HEADER + 0x8)

/* Req Mailboxes */
#define PRCM_REQ_MB0			0x208 /* 12 bytes  */
#define PRCM_REQ_MB1			0x1FC /* 12 bytes  */
#define PRCM_REQ_MB2			0x1EC /* 16 bytes  */
#define PRCM_REQ_MB3			0x78  /* 372 bytes  */
#define PRCM_REQ_MB4			0x74  /* 4 bytes  */
#define PRCM_REQ_MB5			0x70  /* 4 bytes  */
#define PRCM_REQ_PASR			0x30 /* 4 bytes */

/* Ack Mailboxes */
#define PRCM_ACK_MB0			0x34 /* 52 bytes  */
#define PRCM_ACK_MB1			0x30 /* 4 bytes */
#define PRCM_ACK_MB2			0x2C /* 4 bytes */
#define PRCM_ACK_MB3			0x28 /* 4 bytes */
#define PRCM_ACK_MB4			0x24 /* 4 bytes */
#define PRCM_ACK_MB5			0x20 /* 4 bytes */

/* Ack Mailboxe sizes */
#define PRCM_ACK_MB0_SIZE		0x24 /* 52 bytes  */
#define PRCM_ACK_MB1_SIZE		0x4 /* 4 bytes */
#define PRCM_ACK_MB2_SIZE		0x1 /* 1 bytes */
#define PRCM_ACK_MB3_SIZE		0x2 /* 2 bytes */
#define PRCM_ACK_MB4_SIZE		0x0 /* 0 bytes */
#define PRCM_ACK_MB5_SIZE		0x4 /* 4 bytes */

/**
 * struct ux500_mbox - state container for the Ux500 mailbox
 * @mbox_base: remapped base address of the mailbox controller
 * @tcdm_mem_base: remapped base address of the TCDM memory
 * @irq_domain: irq domain for the mailbox
 * @irq_mask: mask for the mailbox channel IRQs
 * @upap_mask: mask for the UPAP channel IRQs
 * @lock: spinlock for this struct
 * @controller: mailbox controller device
 */
struct ux500_mbox {
	void __iomem *base;
	void __iomem *tcdm;
	struct irq_domain *irq_domain;
	u8 irqmask;
	u8 upap_mask;
	spinlock_t lock;
	struct mbox_controller controller;
};

static inline struct ux500_mbox *to_ux500_mbox(struct mbox_controller *cont)
{
	return container_of(cont, struct ux500_mbox, controller);
}

static DEFINE_SPINLOCK(prcm_mbox_irqs_lock);

struct dbx500_chan_priv {
	int			access_mode;
	int			type;
	int			header_size;
	unsigned int		priv_tx_header_offset;
	unsigned int		priv_rx_header_offset;
	unsigned int		priv_tx_offset;
	unsigned int		priv_rx_offset;
	unsigned int		tx_header_offset;
	unsigned int		rx_header_offset;
	unsigned int		tx_offset;
	unsigned int		rx_offset;
	unsigned int		rx_size;
	unsigned int		sw_irq;
	bool			empty;
	struct semaphore	sem;
};

static inline u32 mbox_read_reg(struct ux500_mbox *mbox, size_t ofs)
{
	return readl_relaxed(mbox->base + ofs);
}

static inline void mbox_write_reg(struct ux500_mbox *mbox, size_t ofs,
				  u32 val, size_t ofs)
{
	writel_relaxed(val, mbox->base + ofs);
}

static struct irq_chip dbx500_mbox_irq_chip;

/* Mailbox configuration */
static void dbx500_mbox_configure(struct mbox_chan *chan, void* data)
{
	struct dbx500_chan_priv *priv = chan->priv;
	u32 offset = (u32)data;

	if (priv->type == MAILBOX_LEGACY) {
		priv->rx_offset = priv->priv_rx_offset + offset;
		priv->rx_header_offset = priv->priv_rx_header_offset + offset;
		priv->tx_offset = priv->priv_tx_offset + offset;
		priv->tx_header_offset = priv->priv_tx_header_offset + offset;
	} else if (priv->type == MAILBOX_UPAP) {
		priv->tx_offset = offset;
		priv->rx_offset = offset;
	}
}

/* Mailbox IRQ handle functions */

static void dbx500_mbox_enable_irq(struct mbox_chan *chan, mailbox_irq_t irq)
{
	struct ux500_mbox *mbox = to_ux500_mbox(chan->mbox);
	unsigned long flags;

	spin_lock_irqsave(&mbox->lock, flags);
	mbox->irq_mask |= MBOX_BIT(mbox->id);
	spin_unlock_irqrestore(&mbox->lock, flags);

}

static void dbx500_mbox_disable_irq(struct mbox_chan *chan, mailbox_irq_t irq)
{
	struct ux500_mbox *mbox = to_ux500_mbox(chan->mbox);
	unsigned long flags;

	spin_lock_irqsave(&mbox->lock, flags);
	mbox->irq_mask &= ~MBOX_BIT(mbox->id);
	spin_unlock_irqrestore(&mbox->lock, flags);

}

static void dbx500_mbox_upap_disable_irq(struct mbox_chan *chan,
		mailbox_irq_t irq){}

static void dbx500_mbox_ack_irq(struct mbox_chan *chan, mailbox_irq_t irq)
{
	struct ux500_mbox *mbox = to_ux500_mbox(chan->mbox);
	struct dbx500_chan_priv *priv = chan->con_priv;

	if (irq == IRQ_RX) {
		mbox_write_reg(mbox, MBOX_BIT(chan->id), PRCM_ARM_IT1_CLR);
		if (priv->access_mode == MAILBOX_BLOCKING)
			up(&priv->sem);
	}
}

static void dbx500_mbox_pasr_ack_irq(struct mbox_chan *chan, mailbox_irq_t irq)
{
	return;
}

static void dbx500_mbox_upap_ack_irq(struct mbox_chan *chan, mailbox_irq_t irq)
{
	struct ux500_mbox *mbox = to_ux500_mbox(chan->mbox);
	struct dbx500_chan_priv *priv = chan->con_priv;

	if (irq == IRQ_RX) {
		writel(0, mbox->tcdm + priv->rx_offset);
	}
}

static int dbx500_mbox_is_irq(struct mbox_chan *chan, mailbox_irq_t irq)
{
	struct ux500_mbox *mbox = to_ux500_mbox(chan->mbox);
	struct dbx500_chan_priv *priv = chan->con_priv;

	if (irq == IRQ_RX) {
		if(mbox_read_reg(mbox, PRCM_ARM_IT1_VAL) & MBOX_BIT(chan->id)) {
			priv->empty = false;
			return 1;
		}
		else {
			return 0;
		}
	} else {
		return 0;
	}
}

static int dbx500_mbox_pasr_is_irq(struct mbox_chan *chan, mailbox_irq_t irq)
{
	struct ux500_mbox *mbox = to_ux500_mbox(chan->mbox);

	if (irq == IRQ_RX)
		return mbox_read_reg(mbox, PRCM_MBOX_CPU_VAL) &
			MBOX_BIT(chan->id);
	else
		return 0;
}

static int dbx500_mbox_upap_is_irq(struct mbox_chan *chan, mailbox_irq_t irq)
{
	struct ux500_mbox *mbox = to_ux500_mbox(chan->mbox);
	struct dbx500_chan_priv *priv = chan->con_priv;

	if (irq == IRQ_RX) {
		if(readl(mbox->tcdm + priv->rx_offset) == priv->sw_irq) {
			priv->empty = false;
			return 1;
		}
		else {
			return 0;
		}
	} else {
		return 0;
	}
}

static bool dbx500_mbox_is(struct mbox_chan *chan, int type)
{
	struct dbx500_chan_priv *priv = chan->con_priv;

	return (priv->access_mode == type);
}

/* message management */

static int dbx500_mbox_is_ready(struct mbox_chan *chan)
{
	struct ux500_mbox *mbox = to_ux500_mbox(chan->mbox);

	return mbox_read_reg(mbox, PRCM_MBOX_CPU_VAL) & MBOX_BIT(chan->id);
}

static int dbx500_mbox_write(struct mbox_chan *chan, struct mailbox_msg *msg)
{
	struct ux500_mbox *mbox = to_ux500_mbox(chan->mbox);
	struct dbx500_chan_priv *priv = chan->con_priv;
	int j;

	if (msg->size && !msg->pdata)
		return -EINVAL;

	if (priv->access_mode == MAILBOX_BLOCKING)
		down(&priv->sem);

	while (dbx500_mbox_is_ready(chan))
		cpu_relax();

	/* write header */
	if (priv->header_size)
		writeb(msg->header, mbox->tcdm + priv->tx_header_offset);

	/* write data */
	for (j = 0; j < msg->size; j++)
		writeb(((u8 *)msg->pdata)[j],
				mbox->tcdm + priv->tx_offset + j);

	/* send event */
	mbox_write_reg(mbox, MBOX_BIT(chan->id), PRCM_MBOX_CPU_SET);

	return 0;
}

static int dbx500_mbox_read(struct mbox_chan *chan, struct mailbox_msg *msg)
{
	struct ux500_mbox *mbox = to_ux500_mbox(chan->mbox);
	struct dbx500_chan_priv *priv = chan->con_priv;

	msg->header = readb(mbox->tcdm + priv->rx_header_offset);
	msg->pdata = (u8 *)(mbox->tcdm + priv->rx_offset);

	msg->size = priv->rx_size;
	priv->empty = true;
	return 0;
}

static bool dbx500_mbox_fifo_empty(struct mbox_chan *chan)
{
	struct dbx500_chan_priv *priv = chan->con_priv;

	return priv->empty;
}

static int dbx500_mbox_poll_for_space(struct mbox_chan *mbox)
{
	return 0;
}
/*  interrupt management */

/*  mask/unmask must be managed by SW */

static void mbox_irq_mask(struct irq_data *d)
{
	struct ux500_mbox *mbox = irq_data_get_irq_chip_data(d);
	unsigned long flags;

	spin_lock_irqsave(&mbox->lock, flags);
	mbox->irq_mask &= ~MBOX_BIT(d->hwirq);
	spin_unlock_irqrestore(&mbox->lock, flags);
}

static void mbox_irq_unmask(struct irq_data *d)
{
	struct ux500_mbox *mbox = irq_data_get_irq_chip_data(d);
	unsigned long flags;

	spin_lock_irqsave(&mbox->lock, flags);
	mbox->irq_mask |= MBOX_BIT(d->hwirq);
	spin_unlock_irqrestore(&mbox->lock, flags);
}

static void mbox_irq_ack(struct irq_data *d)
{
	struct ux500_mbox *mbox = irq_data_get_irq_chip_data(d);

	mbox_write_reg(mbox, MBOX_BIT(d->hwirq), PRCM_ARM_IT1_CLR);
}

static struct irq_chip dbx500_mbox_irq_chip = {
	.name		= "dbx500_mbox",
	.irq_disable	= mbox_irq_unmask,
	.irq_ack	= mbox_irq_ack,
	.irq_mask	= mbox_irq_mask,
	.irq_unmask	= mbox_irq_unmask,
};

static int dbx500_mbox_irq_map(struct irq_domain *d, unsigned int irq,
			       irq_hw_number_t hwirq)
{
	struct ux500_mbox *mbox = d->host_data;

	irq_set_chip_data(irq, mbox);
	irq_set_chip_and_handler(irq, &dbx500_mbox_irq_chip,
				 handle_simple_irq);
#ifdef CONFIG_ARM /* FIXME: not for kernel 4.3+ */
	set_irq_flags(irq, IRQF_VALID);
#endif

	return 0;
}

static struct irq_domain_ops dbx500_mbox_irq_ops = {
	.map    = dbx500_mbox_irq_map,
	.xlate  = irq_domain_xlate_twocell,
};

static irqreturn_t dbx500_mbox_irq_handler(int irq, void *data)
{
	struct ux500_mbox *mbox = data;
	u32 bits;
	u8 n;

	bits = (mbox_read_reg(PRCM_ARM_IT1_VAL) & ALL_MBOX_BITS);
	if (unlikely(!bits))
		return IRQ_NONE;

	bits &= mbox->irq_mask;

	for (n = 0; bits; n++) {
		if (bits & MBOX_BIT(n)) {
			unsigned mbox_irq;

			if (mbox->upap_mask & MBOX_BIT(n))
				mbox_write_reg(MBOX_BIT(n), PRCM_ARM_IT1_CLR);
			bits -= MBOX_BIT(n);
			mbox_irq = irq_find_mapping(mbox->irq_domain, n);
			generic_handle_irq(mbox_irq);
		}
	}
	return IRQ_HANDLED;
}

/* 5 mailboxes AP <--> PRCMU */
static struct mailbox_ops dbx500_mbox_ops = {
	.type		= MBOX_SHARED_MEM_TYPE,
	.enable_irq	= dbx500_mbox_enable_irq,
	.disable_irq	= dbx500_mbox_disable_irq,
	.ack_irq	= dbx500_mbox_ack_irq,
	.is_irq		= dbx500_mbox_is_irq,
	.read		= dbx500_mbox_read,
	.write		= dbx500_mbox_write,
	.fifo_empty	= dbx500_mbox_fifo_empty,
	.poll_for_space = dbx500_mbox_poll_for_space,
	.is_mbox	= dbx500_mbox_is,
	.configure	= dbx500_mbox_configure,
};

static struct dbx500_chan_priv mbox0_priv = {
	.access_mode		= MAILBOX_ATOMIC,
	.type			= MAILBOX_LEGACY,
	.priv_tx_header_offset	= PRCM_MBOX_HEADER_REQ_MB0,
	.header_size		= 1,
	.priv_tx_offset		= PRCM_REQ_MB0,
	.priv_rx_header_offset	= PRCM_MBOX_HEADER_ACK_MB0,
	.priv_rx_offset		= PRCM_ACK_MB0,
	.rx_size		= PRCM_ACK_MB0_SIZE,
};

static struct mbox_chan mbox0_info = {
	.name	= "mbox0",
	.id	= 0,
	.ops	= &dbx500_mbox_ops,
	.con_priv = &mbox0_priv,
};

static struct dbx500_chan_priv mbox1_priv = {
	.access_mode		= MAILBOX_BLOCKING,
	.type			= MAILBOX_LEGACY,
	.priv_tx_header_offset	= PRCM_MBOX_HEADER_REQ_MB1,
	.header_size		= 1,
	.priv_tx_offset		= PRCM_REQ_MB1,
	.priv_rx_header_offset	= PRCM_MBOX_HEADER_REQ_MB1,
	.priv_rx_offset		= PRCM_ACK_MB1,
	.rx_size		= PRCM_ACK_MB1_SIZE,
};

static struct mbox_chan mbox1_info = {
	.name	= "mbox1",
	.id	= 1,
	.ops	= &dbx500_mbox_ops,
	.con_priv = &mbox1_priv,
};

static struct dbx500_chan_priv mbox2_priv = {
	.access_mode		= MAILBOX_BLOCKING,
	.type			= MAILBOX_LEGACY,
	.priv_tx_header_offset	= PRCM_MBOX_HEADER_REQ_MB2,
	.header_size		= 1,
	.priv_tx_offset		= PRCM_REQ_MB2,
	.priv_rx_header_offset	= PRCM_MBOX_HEADER_REQ_MB2,
	.priv_rx_offset		= PRCM_ACK_MB2,
	.rx_size		= PRCM_ACK_MB2_SIZE,
};

static struct mbox_chan mbox2_info = {
	.name	= "mbox2",
	.id	= 2,
	.ops	= &dbx500_mbox_ops,
	.con_priv = &mbox2_priv,
};

static struct dbx500_chan_priv mbox3_priv = {
	.access_mode		= MAILBOX_BLOCKING_NOTIF,
	.type			= MAILBOX_LEGACY,
	.priv_tx_header_offset	= PRCM_MBOX_HEADER_REQ_MB3,
	.header_size		= 1,
	.priv_tx_offset		= PRCM_REQ_MB3,
	.priv_rx_header_offset	= PRCM_MBOX_HEADER_REQ_MB3,
	.priv_rx_offset		= PRCM_ACK_MB3,
	.rx_size		= PRCM_ACK_MB3_SIZE,
};

static struct mbox_chan mbox3_info = {
	.name	= "mbox3",
	.id	= 3,
	.ops	= &dbx500_mbox_ops,
	.con_priv = &mbox3_priv,
};

static struct dbx500_chan_priv mbox4_priv = {
	.access_mode		= MAILBOX_BLOCKING,
	.type			= MAILBOX_LEGACY,
	.priv_tx_header_offset	= PRCM_MBOX_HEADER_REQ_MB4,
	.header_size		= 1,
	.priv_tx_offset		= PRCM_REQ_MB4,
	.priv_rx_header_offset	= PRCM_MBOX_HEADER_REQ_MB4,
	.priv_rx_offset		= PRCM_ACK_MB4,
	.rx_size		= PRCM_ACK_MB4_SIZE,
};

static struct mbox_chan mbox4_info = {
	.name	= "mbox4",
	.id	= 4,
	.ops	= &dbx500_mbox_ops,
	.con_priv = &mbox4_priv,
};

static struct dbx500_chan_priv mbox5_priv = {
	.access_mode		= MAILBOX_BLOCKING,
	.type			= MAILBOX_LEGACY,
	.priv_tx_header_offset	= PRCM_MBOX_HEADER_REQ_MB5,
	.header_size		= 1,
	.priv_tx_offset		= PRCM_REQ_MB5,
	.priv_rx_header_offset	= PRCM_MBOX_HEADER_REQ_MB5,
	.priv_rx_offset		= PRCM_ACK_MB5,
	.rx_size		= PRCM_ACK_MB5_SIZE,
};

static struct mbox_chan mbox5_info = {
	.name	= "mbox5",
	.id	= 5,
	.ops	= &dbx500_mbox_ops,
	.con_priv = &mbox5_priv,
};

static struct mbox_chan mbox6_info = {
	.name	= "mbox6",
	.id	= 6,
	.ops	= &dbx500_mbox_ops,
};

static struct mbox_chan mbox7_info = {
	.name	= "mbox7",
	.id	= 7,
	.ops	= &dbx500_mbox_ops,
};

/* pasr interface */
static struct mailbox_ops dbx500_mbox_pasr_ops = {
	.type		= MBOX_SHARED_MEM_TYPE,
	.enable_irq	= dbx500_mbox_enable_irq,
	.disable_irq	= dbx500_mbox_disable_irq,
	.ack_irq	= dbx500_mbox_pasr_ack_irq,
	.is_irq		= dbx500_mbox_pasr_is_irq,
	.read		= dbx500_mbox_read,
	.write		= dbx500_mbox_write,
	.fifo_empty	= dbx500_mbox_fifo_empty,
	.poll_for_space = dbx500_mbox_poll_for_space,
	.is_mbox	= dbx500_mbox_is,
	.configure	= dbx500_mbox_configure,
};

static struct dbx500_chan_priv mbox0_pasr_priv = {
	.access_mode		= MAILBOX_ATOMIC,
	.type			= MAILBOX_LEGACY,
	.priv_tx_header_offset	= PRCM_MBOX_HEADER_REQ_MB0,
	.header_size		= 1,
	.priv_tx_offset		= PRCM_REQ_PASR,
	.priv_rx_header_offset	= PRCM_MBOX_HEADER_REQ_MB0,
	.priv_rx_offset		= PRCM_ACK_MB0,
	.rx_size		= PRCM_ACK_MB0_SIZE,
};

static struct mbox_chan mbox0_pasr_info = {
	.name	= "mbox0_pasr",
	.id	= 0,
	.ops	= &dbx500_mbox_pasr_ops,
	.con_priv = &mbox0_pasr_priv,
};

/* x540 mailbox definition */
static struct mailbox_ops dbx500_mbox_upap_ops = {
	.type		= MBOX_SHARED_MEM_TYPE,
	.startup	= dbx500_mbox_startup,
	.enable_irq	= dbx500_mbox_enable_irq,
	.disable_irq	= dbx500_mbox_upap_disable_irq,
	.ack_irq	= dbx500_mbox_upap_ack_irq,
	.is_irq		= dbx500_mbox_upap_is_irq,
	.read		= dbx500_mbox_read,
	.write		= dbx500_mbox_write,
	.fifo_empty	= dbx500_mbox_fifo_empty,
	.poll_for_space = dbx500_mbox_poll_for_space,
	.is_mbox	= dbx500_mbox_is,
	.configure	= dbx500_mbox_configure,
};

static struct dbx500_chan_priv mbox1_upap_req_priv = {
	.access_mode	= MAILBOX_BLOCKING_NOTIF,
	.type		= MAILBOX_UPAP,
	.priv_tx_header_offset	= 0,
	.header_size	= 0,
	.priv_tx_offset	= 0,
	.priv_rx_offset	= 0,
	.rx_size	= 0x28,
	.sw_irq		= 0x3,
};

static struct mbox_chan mbox1_upap_req_info = {
	.name	= "mbox1_upap_req",
	.id	= 1,
	.ops	= &dbx500_mbox_upap_ops,
	.con_priv = &mbox1_upap_req_priv,
};

static struct dbx500_chan_priv mbox1_upap_nfy_priv = {
	.access_mode	= MAILBOX_BLOCKING_NOTIF,
	.type		= MAILBOX_UPAP,
	.priv_tx_header_offset	= 0,
	.header_size	= 0,
	.priv_tx_offset	= 0,
	.priv_rx_offset	= 0x28,
	.rx_size	= 0x18,
	.sw_irq		= 0x2,
};

static struct mbox_chan mbox1_upap_nfy_info = {
	.name	= "mbox1_upap_nfy",
	.id	= 1,
	.ops	= &dbx500_mbox_upap_ops,
	.con_priv = &mbox1_upap_nfy_priv,
};

static struct mbox_chan *db8500_chans[] = { &mbox0_info, &mbox1_info,
	&mbox2_info, &mbox3_info, &mbox4_info, &mbox5_info, &mbox6_info,
	&mbox7_info, &mbox0_pasr_info, NULL };

static struct mbox_chan *dbx540_chans[] = { &mbox0_info, &mbox2_info,
	&mbox3_info, &mbox4_info, &mbox5_info, &mbox6_info, &mbox7_info,
	&mbox1_upap_req_info, &mbox1_upap_nfy_info, &mbox0_pasr_info, NULL };

static const struct of_device_id dbx500_mailbox_match[] = {
        { .compatible = "stericsson,db8500-mailbox",
		.data = (void *)db8500_chans,
	},
        { .compatible = "stericsson,db8540-mailbox",
		.data = (void *)dbx540_chans,
	},
        { .compatible = "stericsson,db9540-mailbox",
		.data = (void *)dbx540_chans,
	},
	{ /* sentinel */}
};

static int dbx500_mbox_send_data(struct mbox_chan *chan, void *data)
{
	struct ux500_mbox *mbox = to_ux500_mbox(chan->mbox);
	struct dbx500_chan_priv *priv = chan->con_priv;

	return 0;
}

static int dbx500_mbox_startup(struct mbox_chan *chan)
{
	return 0;
}

static int dbx500_mbox_shutdown(struct mbox_chan *chan)
{
	return 0;
}

static struct mbox_chan_ops dbx500_mbox_chan_ops = {
	.send_data	= dbx500_mbox_send_data,
	.startup	= dbx500_mbox_startup,
	.shutdown	= dbx500_mbox_shutdown,
	/* No .last_tx_done or .peek_data as we're using IRQ */
};

static int dbx500_mbox_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *mem;
	int ret, i;
	u32 legacy_offset;
	u32 upap_offset;
	struct mbox_chan **list;
	int irq;
	struct device_node *np = pdev->dev.of_node;
	struct ux500_mbox *mbox;

	mbox = devm_kzalloc(dev, sizeof(*mbox), GFP_KERNEL);
        if (!mbox)
		return -ENOMEM;

	list = (struct mbox_chan **)of_match_device(
			dbx500_mailbox_match, dev)->data;
	if (!list) {
		dev_err(dev, "No channel list found\n");
		return -ENODEV;
	}
	of_property_read_u32(np, "legacy-offset", &legacy_offset);
	of_property_read_u32(np, "upap-offset", &upap_offset);

	mem = platform_get_resource_byname(pdev, IORESOURCE_MEM, "prcm-reg");
	mbox->mbox_base = devm_ioremap(dev, mem->start, resource_size(mem));
	if (!mbox_base)
		return -ENOMEM;

	mem = platform_get_resource_byname(pdev, IORESOURCE_MEM, "prcmu-tcdm");
	mbox->tcdm = devm_ioremap(dev, mem->start, resource_size(mem));
	if (!mbox->tcdm)
		return -ENOMEM;

	irq = platform_get_irq(pdev, 0);
	/* clean all mailboxes */
	mbox_write_reg(ALL_MBOX_BITS, PRCM_ARM_IT1_CLR);
	ret = devm_request_irq(dev, irq, dbx500_mbox_irq_handler,
			IRQF_NO_SUSPEND, "dbx500-mbox", mbox);
	if (ret)
		return ret;

	/*
	 * TODO: rewrite like this when we get SPARSE IRQ:
	 *	dbx500_mbox_irq_domain = irq_domain_add_linear(
	 *	np, NUM_MB, &dbx500_mbox_irq_ops, NULL);
	 */
	mbox->irq_domain = irq_domain_add_simple(
		np, NUM_MB, 0, &dbx500_mbox_irq_ops, mbox);
	spin_lock_init(&mbox->lock);

	if (!mbox->irq_domain) {
		dev_err(dev, "Failed to create irqdomain\n");
		return -ENOSYS;
	}

	mbox->controller.dev = dev;
	mbox->controller.ops = &ux500_mbox_chan_ops;
	mbox->controller.txdone_irq = true;
	mbox->controller.chans = list;

	/*
	 * Update mailbox shared memory buffer offset according to mailbox
	 * type
	 * init semaphore
	 */
	for (i = 0; list[i]; i++) {
		struct mbox_chan *chan = list[i];
		struct dbx500_chan_priv *priv =
			(struct dbx500_chan_priv *)chan->con_priv;

		mbox->controller.num_chans++;
		if (!priv)
			continue;
		chan->irq = irq_create_mapping(mbox->irq_domain, chan->id);
		if (priv->type == MAILBOX_LEGACY) {
			priv->rx_offset = priv->priv_rx_offset + legacy_offset;
			priv->rx_header_offset = priv->priv_rx_header_offset
				+ legacy_offset;
			priv->tx_offset = priv->priv_tx_offset + legacy_offset;
			priv->tx_header_offset = priv->priv_tx_header_offset
				+ legacy_offset;
		} else if (priv->type == MAILBOX_UPAP) {
			priv->tx_offset = priv->priv_tx_offset + upap_offset;
			priv->rx_offset = priv->priv_rx_offset + upap_offset;
			mbox->upap_mask |= MBOX_BIT(chan->id);
		}
		sema_init(&priv->sem, 1);
	}

	ret = mbox_controller_register(&mbox->controller);
	if (ret)
		return ret;
	dev_info(dev, "DBx500 PRCMU mailbox driver registered\n");

	return 0;
}

static struct platform_driver dbx500_mbox_driver = {
	.driver = {
		.name = "dbx500-mailbox",
		.of_match_table = dbx500_mailbox_match,
		.suppress_bind_attrs = true,
	},
};

static int __init dbx500_mbox_init(void)
{
	return platform_driver_probe(&dbx500_mbox_driver, dbx500_mbox_probe);
}
postcore_initcall(dbx500_mbox_init);
