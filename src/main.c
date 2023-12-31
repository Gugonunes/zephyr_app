/*
 * Copyright (c) 2017 Linaro Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/sys/util.h>
#include <zephyr/shell/shell.h>
#include <string.h>
#include <inttypes.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/kernel.h>

#define ZEPHYR_USER_NODE DT_PATH(zephyr_user)
#define CONFIG_SYS_CLOCK_TICKS_PER_SEC 1000
#define CONFIG_NUM_PREEMPT_PRIORITIES 15
#define ARCH_STACK_PTR_ALIGN 8

#define LED0_NODE DT_ALIAS(led0)
#define LED1_NODE DT_ALIAS(led1)

#define SW0_NODE DT_ALIAS(sw0)
#if !DT_NODE_HAS_STATUS(SW0_NODE, okay)
#error "Unsupported board: sw0 devicetree alias is not defined"
#endif

/* size of stack area used by each thread */
#define STACKSIZE 1024

/* scheduling priority used by each thread */
#define PRIORITY 7

#if !DT_NODE_HAS_STATUS(LED0_NODE, okay)
#error "Unsupported board: led0 devicetree alias is not defined"
#endif


#if !DT_NODE_HAS_STATUS(LED1_NODE, okay)
#error "Unsupported board: led1 devicetree alias is not defined"
#endif


struct led {
	struct gpio_dt_spec spec;
	uint8_t num;
};

static const struct led led0 = {
	.spec = GPIO_DT_SPEC_GET_OR(LED0_NODE, gpios, {0}),
	.num = 0,
};

// --------------------------------------------

// --------------------------------------------
// Gerenciamento de um botão e declaraçao do zbus
// O zbus é utilizado aqui para validar se o número é par ou ímpar

static const struct gpio_dt_spec button =
    GPIO_DT_SPEC_GET_OR(SW0_NODE, gpios, {0});
static struct gpio_callback button_cb_data;
static int32_t button_pressed_count = 0;


ZBUS_CHAN_DEFINE(count_button, int32_t, NULL, NULL,
                  ZBUS_OBSERVERS(thread_handler1_sub),
                  ZBUS_MSG_INIT(0)
);

ZBUS_CHAN_DECLARE(count_button);
ZBUS_SUBSCRIBER_DEFINE(thread_handler1_sub, 4);

// --------------------------------------------
// Comando personalizado para validar
static int cmd_validar(const struct shell *shell, size_t argc, char** argv) {
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
  const struct zbus_chan *chan;

	while (!zbus_sub_wait(&thread_handler1_sub, &chan, K_MSEC(200))) {
		int msg;

		zbus_chan_read(chan, &msg, K_MSEC(1000));
    if(msg %2 == 0)
    {
		    printk("O valor do contador é: %d\nE é um valor par\n", msg);
    } else {
        printk("O valor do contador é: %d\nE é um valor ímpar\n", msg);
    }
	}
  return 0;
}

SHELL_CMD_ARG_REGISTER(validar, NULL, "Description: validate number", cmd_validar, 1, 0);

static void button_pressed(const struct device *dev, struct gpio_callback *cb,
                           uint32_t pins) {
  button_pressed_count++;
  printk("%" PRIu32 "\n", button_pressed_count);
  zbus_chan_pub(&count_button, &button_pressed_count, K_MSEC(1000));
}

int button_thread(void) {
  int ret;
  if (!gpio_is_ready_dt(&button)) {
    printk("Error: button device %s is not ready\n", button.port->name);
    return 0;
  }

  ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
  if (ret != 0) {
    printk("Error %d: failed to configure %s pin %d\n", ret, button.port->name,
           button.pin);
    return 0;
  }

  ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
  if (ret != 0) {
    printk("Error %d: failed to configure interrupt on %s pin %d\n", ret,
           button.port->name, button.pin);
    return 0;
  }

  gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
  gpio_add_callback(button.port, &button_cb_data);
  printk("Botão inicializado em %s, pin %d\n", button.port->name, button.pin);

  printk("Pressione o botão para começar a contar.\n");

  while (1) {
      k_msleep(100);
  }

  return 0;
}

// --------------------------------------------
// Comando personalizado para gerar um sinal DAC
static int cmd_dac(const struct shell *shell, size_t argc, char** argv) {
  ARG_UNUSED(argc);
  ARG_UNUSED(argv);
  printk("Gerando sinal DAC\n");
  // funcao que gera o DAC
  
  return 0;
}

SHELL_CMD_ARG_REGISTER(dac, NULL, "Description: create dac", cmd_dac, 1, 0);

// --------------------------------------------
// Led de atividade com softtimer usando k work

void blink(const struct led *led, uint32_t sleep_ms, uint32_t id)
{
	const struct gpio_dt_spec *spec = &led->spec;
	int ret;
	int cnt = 0;

	if (!device_is_ready(spec->port)) {
		printk("Error: %s device is not ready\n", spec->port->name);
		return;
	}

	ret = gpio_pin_configure_dt(spec, GPIO_OUTPUT);
	if (ret != 0) {
		printk("Error %d: failed to configure pin %d (LED '%d')\n",
			ret, spec->pin, led->num);
		return;
	}

	while (1) {
		gpio_pin_set(spec->port, spec->pin, cnt % 2);
		cnt++;

		k_msleep(sleep_ms);
	}
}
K_WORK_DEFINE(blink_work, blink);
void blink_handler(struct k_work *work)
{
    blink(&led0, 1000, 0);
}

void blink0_work_init(void)
{
    k_work_init(&blink_work, blink_handler);
}

void blink_scheduler(struct k_work *work)
{
    blink_handler(work);
    k_work_reschedule(&blink_work, K_MSEC(1000));
}

// --------------------------------------------


// thread do botão
K_THREAD_DEFINE(button_thread_id, STACKSIZE, button_thread, NULL, NULL, NULL,
                PRIORITY, 0, 0);

// k work do led de atividade
K_WORK_DEFINE(blink_scheduler_work, blink_scheduler);

int main(void)
{
  printk("Ligando os motores vrummm\n");
	blink0_work_init();

  k_work_submit(&blink_scheduler_work);
  
  while (1) {
      k_sleep(K_FOREVER);
  }
	return 0;
}
