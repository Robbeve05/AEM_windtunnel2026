#include "AppConfig.h"
#include "esp_eth.h"
#include "esp_netif.h"
#include "esp_eth_netif_glue.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "TaskData.h"

static void ethernet_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base==ETH_EVENT && id==ETHERNET_EVENT_DISCONNECTED)
        xEventGroupSetBits(events,STOP_REQUEST);
    if (base==IP_EVENT && id==IP_EVENT_ETH_GOT_IP) {
        ip_event_got_ip_t *event=data;
        ESP_LOGI("Ethernet","Pi TCP port %d, IP " IPSTR,PI_TCP_PORT,IP2STR(&event->ip_info.ip));
    }
}
void Ethernet_Init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_config_t netconfig=ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *netif=esp_netif_new(&netconfig);
    ESP_ERROR_CHECK(netif ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    spi_bus_config_t bus={.mosi_io_num=ETH_MOSI_PIN,.miso_io_num=ETH_MISO_PIN,
        .sclk_io_num=ETH_SCLK_PIN,.quadwp_io_num=-1,.quadhd_io_num=-1};
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST,&bus,SPI_DMA_CH_AUTO));
    spi_device_interface_config_t device={.mode=0,.clock_speed_hz=20000000,
        .spics_io_num=ETH_CS_PIN,.queue_size=20};
    eth_w5500_config_t chip=ETH_W5500_DEFAULT_CONFIG(SPI2_HOST,&device);
    chip.int_gpio_num=ETH_INT_PIN;
    eth_mac_config_t macconfig=ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phyconfig=ETH_PHY_DEFAULT_CONFIG();
    phyconfig.reset_gpio_num=ETH_RESET_PIN;
    phyconfig.phy_addr=1;
    esp_eth_mac_t *mac=esp_eth_mac_new_w5500(&chip,&macconfig);
    esp_eth_phy_t *phy=esp_eth_phy_new_w5500(&phyconfig);
    ESP_ERROR_CHECK(mac && phy ? ESP_OK : ESP_ERR_NO_MEM);
    esp_eth_config_t config=ETH_DEFAULT_CONFIG(mac,phy);
    esp_eth_handle_t handle;
    ESP_ERROR_CHECK(esp_eth_driver_install(&config,&handle));
    uint8_t address[6];
    ESP_ERROR_CHECK(esp_read_mac(address,ESP_MAC_ETH));
    ESP_ERROR_CHECK(esp_eth_ioctl(handle,ETH_CMD_S_MAC_ADDR,address));
    ESP_ERROR_CHECK(esp_netif_attach(netif,esp_eth_new_netif_glue(handle)));
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT,ESP_EVENT_ANY_ID,ethernet_event,NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,IP_EVENT_ETH_GOT_IP,ethernet_event,NULL));
    ESP_ERROR_CHECK(esp_eth_start(handle));
}
