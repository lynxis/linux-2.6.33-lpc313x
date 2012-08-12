/*  arch/arm/mach-lpc313x/time.c
 *
 *  Author:	Durgesh Pattamatta
 *  Copyright (C) 2009 NXP semiconductors
 *
 *  Timer driver for LPC313x & LPC315x.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/time.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>


#include <mach/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/leds.h>

#include <asm/mach/time.h>
#include <mach/gpio.h>
#include <mach/board.h>
//#include <mach/cgu.h>

struct lpc313x_timer {
	/* id of timer */
	int id;
	/* physical base */
	unsigned long phys_base;
	/* CGU clock id */
	int clk_id;
	/* irq number */
	int irq;

	char *descr;

	/* timer reserved for static use */
	unsigned reserved:1;
	/* timer currently allocated */
	unsigned used:1;
	/* timer undergoing oneshot operation */
	unsigned oneshot:1;
};

#define TIMER_IO_SIZE (SZ_1K)

struct lpc313x_timer timers[] = {
	{.phys_base = TIMER0_PHYS, .clk_id = CGU_SB_TIMER0_PCLK_ID, .irq = IRQ_TIMER0 },
	{.phys_base = TIMER1_PHYS, .clk_id = CGU_SB_TIMER1_PCLK_ID, .irq = IRQ_TIMER1 },
	{.phys_base = TIMER2_PHYS, .clk_id = CGU_SB_TIMER2_PCLK_ID, .irq = IRQ_TIMER2 },
	{.phys_base = TIMER3_PHYS, .clk_id = CGU_SB_TIMER3_PCLK_ID, .irq = IRQ_TIMER3 },
};

#define NUM_TIMERS (sizeof(timers)/sizeof(struct lpc313x_timer))

void lpc313x_generic_timer_init(void)
{
	int i;
	for(i = 0; i < NUM_TIMERS; i++) {
		struct lpc313x_timer *t = &timers[i];

		t->id = i;

		/* if timer is reserved, mark as used and ignore */
		if(t->reserved) {
			t->used = 1;
			continue;
		}

		/* enable the clock of the timer */
		cgu_clk_en_dis(t->clk_id, 1);

		/* initialize the timer itself */
		TIMER_CONTROL(t->phys_base) = 0;
		TIMER_CLEAR(t->phys_base) = 0;

		/* disable clock again, will be enabled when allocated */
		cgu_clk_en_dis(t->clk_id, 0);
	}
}

struct lpc313x_timer *lpc313x_generic_timer_request(char *descr)
{
	int i;
	struct lpc313x_timer *t = NULL;
	for(i = 0; i < NUM_TIMERS; i++) {
		t = &timers[i];

		if(t->used)
			continue;

		/* enable the clock of the timer */
		cgu_clk_en_dis(t->clk_id, 1);

		/* mark the timer as used */
		t->used = 1;

		/* attach description */
		t->descr = descr;

		break;
	}
	return t;
}

void lpc313x_generic_timer_free(struct lpc313x_timer *t)
{
	TIMER_CONTROL(t->phys_base) = 0;

	cgu_clk_en_dis(t->clk_id, 0);

	t->descr = NULL;
	t->used = 0;
}

int lpc313x_generic_timer_get_irq(struct lpc313x_timer *t)
{
	return t->irq;
}

void lpc313x_generic_timer_ack_irq(struct lpc313x_timer *t)
{
	TIMER_CLEAR(t->phys_base) = 0;
}

u32 lpc313x_generic_timer_get_infreq(struct lpc313x_timer *t)
{
	return cgu_get_clk_freq(t->clk_id);
}

u32 lpc313x_generic_timer_get_value(struct lpc313x_timer *t)
{
	return TIMER_VALUE(t->phys_base);
}

void lpc313x_generic_timer_periodic(struct lpc313x_timer *t, u32 period)
{
	t->oneshot = 0;
	TIMER_CONTROL(t->phys_base) = 0;
	TIMER_LOAD(t->phys_base) = period;
	TIMER_CONTROL(t->phys_base) = TM_CTRL_ENABLE | TM_CTRL_PERIODIC;
	TIMER_CLEAR(t->phys_base) = 0;
}

void lpc313x_generic_timer_oneshot(struct lpc313x_timer *t, u32 duration)
{
	t->oneshot = 1;
	TIMER_CONTROL(t->phys_base) = 0;
	TIMER_LOAD(t->phys_base) = duration;
	TIMER_CONTROL(t->phys_base) = TM_CTRL_ENABLE | TM_CTRL_PERIODIC;
	TIMER_CLEAR(t->phys_base) = 0;	
}

void lpc313x_generic_timer_continuous(struct lpc313x_timer *t)
{
	t->oneshot = 0;
	TIMER_CONTROL(t->phys_base) = 0;
	TIMER_CONTROL(t->phys_base) = TM_CTRL_ENABLE;
	TIMER_CLEAR(t->phys_base) = 0;
}

void lpc313x_generic_timer_stop(struct lpc313x_timer *t)
{
	TIMER_CONTROL(t->phys_base) &= ~TM_CTRL_ENABLE;
}

void lpc313x_generic_timer_continue(struct lpc313x_timer *t)
{
	TIMER_CONTROL(t->phys_base) |= TM_CTRL_ENABLE;
}

#if defined (CONFIG_DEBUG_FS)

static int lpc313x_timers_show(struct seq_file *s, void *v)
{
	int i;
	for(i = 0; i < NUM_TIMERS; i++) {
		struct lpc313x_timer *t = &timers[i];
		int clken;
		u32 ctrl;

		if(t->reserved) {
			seq_printf(s, "timer%d is reserved\n", i);
		}
		if(t->used) {
			seq_printf(s, "timer%d is allocated as \"%s\"\n", i, t->descr);
		}

		clken = cgu_clk_is_enabled(t->clk_id);

		if(!clken) {
			seq_printf(s, "timer%d has clock disabled\n", i);
			continue;
		}

		seq_printf(s, "timer%d input clock running at %d Hz\n", i,
			   lpc313x_generic_timer_get_infreq(t));

		ctrl = TIMER_CONTROL(t->phys_base);

		seq_printf(s, "timer%d is %s in mode %s\n", i,
			   (ctrl & TM_CTRL_ENABLE)?"running":"stopped",
			   (ctrl & TM_CTRL_PERIODIC)?"periodic":"continuous");
		
		if(ctrl & TM_CTRL_PERIODIC) {
			seq_printf(s, "timer%d value 0x%08x load 0x%08x\n", i,
				   TIMER_VALUE(t->phys_base), TIMER_LOAD(t->phys_base));
		} else {
			seq_printf(s, "timer%d value 0x%08x\n", i,
				   TIMER_VALUE(t->phys_base));
		}
	}

	return 0;
}

static int lpc313x_timers_open(struct inode *inode, struct file *file)
{
	return single_open(file, &lpc313x_timers_show, inode->i_private);
}

static const struct file_operations lpc313x_timers_fops = {
	.owner		= THIS_MODULE,
	.open		= lpc313x_timers_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

void __init lpc313x_timer_init_debugfs(void)
{
	struct dentry		*node;

	node = debugfs_create_file("timers", S_IRUSR, NULL, NULL,
			&lpc313x_timers_fops);
	if (IS_ERR(node)) {
		printk("lpc313x_timers_init: failed to init debugfs\n");
	}

	return;
}
#endif

/* Continuous timer counter */

static struct lpc313x_timer *clksource_timer;

static cycle_t clksource_read_cycles(struct clocksource *cs)
{
	u32 c = 0xffffffff - lpc313x_generic_timer_get_value(clksource_timer);
	return (cycle_t)(c);
}

static struct clocksource clksource = {
	.name		= "clksource",
	.rating		= 200,
	.read		= clksource_read_cycles,
	.mask		= CLOCKSOURCE_MASK(32),
	.shift		= 20,
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static void __init lpc313x_clocksource_init(void)
{
	static struct lpc313x_timer *t;
	u32 freq;
	static char err1[] __initdata = KERN_ERR
		"%s: failed to request timer\n";
	static char err2[] __initdata = KERN_ERR
		"%s: can't register clocksource!\n";
	
	t = lpc313x_generic_timer_request("clksource");
	if (!t)
		printk(err1, clksource.name);

	clksource_timer = t;

	freq = lpc313x_generic_timer_get_infreq(t);

	clksource.mult = clocksource_hz2mult(freq, clksource.shift);

	lpc313x_generic_timer_continuous(t);

	if (clocksource_register(&clksource))
		printk(err2, clksource.name);
}

/* Programmable periodic timer */

static struct lpc313x_timer *clkevent_timer;

static int clkevent_set_next_event(unsigned long cycles,
				   struct clock_event_device *evt)
{
	lpc313x_generic_timer_oneshot(clkevent_timer, cycles);

	return 0;
}

static void clkevent_set_mode(enum clock_event_mode mode,
			      struct clock_event_device *evt)
{
	u32 period;
	lpc313x_generic_timer_stop(clkevent_timer);

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		period = lpc313x_generic_timer_get_infreq(clkevent_timer) / HZ;
		lpc313x_generic_timer_periodic(clkevent_timer, period);
		break;
	case CLOCK_EVT_MODE_ONESHOT:
		break;
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
	case CLOCK_EVT_MODE_RESUME:
		break;
	}
}

static struct clock_event_device clkevent = {
	.name		= "clkevent",
	.features       = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.rating         = 200,
	.shift		= 20,
	.set_mode	= clkevent_set_mode,
	.set_next_event = clkevent_set_next_event,
};

static irqreturn_t clkevent_interrupt(int irq, void *dev_id)
{
	struct lpc313x_timer *t = (struct lpc313x_timer *)dev_id;
	struct clock_event_device *evt = &clkevent;

	lpc313x_generic_timer_ack_irq(t);

	if(t->oneshot) {
		t->oneshot = 0;
		lpc313x_generic_timer_stop(t);
	}

	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static struct irqaction clkevent_irq = {
	.name		= "clkevent",
	.flags		= IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler	= clkevent_interrupt,
};

static void __init lpc313x_clockevents_init(void)
{
	static struct lpc313x_timer *t;
	u32 freq;
	static char err1[] __initdata = KERN_ERR
		"%s: failed to request timer\n";

	t = lpc313x_generic_timer_request("clkevent");
	if(!t)
		printk(err1, clkevent.name);

	clkevent_timer = t;

	clkevent_irq.dev_id = (void *)clkevent_timer;

	setup_irq(lpc313x_generic_timer_get_irq(t), &clkevent_irq);

	freq = lpc313x_generic_timer_get_infreq(t);

	clkevent.mult = div_sc(freq, NSEC_PER_SEC, clkevent.shift);

	clkevent.max_delta_ns =
		clockevent_delta2ns(0xffffffff, &clkevent);
	clkevent.min_delta_ns =
		clockevent_delta2ns(60, &clkevent); /* XXX cautious */

	clkevent.cpumask = cpumask_of(0);

	clockevents_register_device(&clkevent);
}

extern int __init cgu_init(void);

static void __init lpc313x_timer_init (void)
{
	/* We need to have clocks set up, so init the CGU here. */
	cgu_init();
	/* Initialize platform timers */
	lpc313x_generic_timer_init();
	/* Set up kernel timers */
	lpc313x_clocksource_init();
	lpc313x_clockevents_init();
}

struct sys_timer lpc313x_timer = {
	.init = lpc313x_timer_init,
};
