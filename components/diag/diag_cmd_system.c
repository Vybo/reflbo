#include <inttypes.h>
#include <stdio.h>

#include "diag_internal.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_console.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static int cmd_version(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    const esp_app_desc_t *app = esp_app_get_description();
    char elf_sha[17];
    esp_app_get_elf_sha256(elf_sha, sizeof(elf_sha));

    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint32_t flash_size = 0;
    if (esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        flash_size = 0;
    }
    size_t psram_size = esp_psram_is_initialized() ? esp_psram_get_size() : 0;

    printf("%s %s\n", app->project_name, app->version);
    printf("built %s %s, esp-idf %s, elf %s\n", app->date, app->time, app->idf_ver, elf_sha);
    printf("chip %s rev v%d.%d, %d cores, flash %" PRIu32 " MB, psram %u MB\n",
           CONFIG_IDF_TARGET, chip.revision / 100, chip.revision % 100, chip.cores,
           flash_size / (1024 * 1024), (unsigned)(psram_size / (1024 * 1024)));
    return 0;
}

static void print_heap(const char *name, uint32_t caps)
{
    printf("%-8s free %8u  min %8u  largest %8u\n", name,
           (unsigned)heap_caps_get_free_size(caps),
           (unsigned)heap_caps_get_minimum_free_size(caps),
           (unsigned)heap_caps_get_largest_free_block(caps));
}

static int cmd_heap(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    print_heap("internal", MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    print_heap("dma", MALLOC_CAP_DMA);
    print_heap("psram", MALLOC_CAP_SPIRAM);
    return 0;
}

static int cmd_tasks(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    static char buf[1024];
    vTaskList(buf);
    printf("Name          State Prio Stack  Num Core\n%s", buf);
    return 0;
}

static int cmd_reboot(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("rebooting\n");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
    return 0;
}

esp_err_t diag_register_system_commands(void)
{
    const esp_console_cmd_t cmds[] = {
        { .command = "version", .help = "Show firmware, ESP-IDF and chip information", .func = &cmd_version },
        { .command = "heap", .help = "Show free heap per memory type", .func = &cmd_heap },
        { .command = "reboot", .help = "Restart the chip", .func = &cmd_reboot },
        { .command = "tasks", .help = "List FreeRTOS tasks", .func = &cmd_tasks },
    };
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        esp_err_t err = esp_console_cmd_register(&cmds[i]);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}
