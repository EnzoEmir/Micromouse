// Host stub of the shared I2C manager. The bus is "always there" and read/write
// calls succeed; the INA226 mock provides the actual sensor readings, so these
// transfers are never exercised for their payload.
#include "i2c_manager.hpp"

static int g_bus_token = 1;

bool i2c_manager_init() { return true; }
i2c_master_bus_handle_t i2c_manager_get_bus() { return &g_bus_token; }
bool i2c_manager_register_device(uint8_t, uint32_t, i2c_master_dev_handle_t* out) {
    if (out) *out = &g_bus_token;
    return true;
}
bool i2c_manager_unregister_device(uint8_t) { return true; }
bool i2c_manager_register_default_devices() { return true; }
i2c_master_dev_handle_t i2c_manager_get_device_handle(uint8_t) { return &g_bus_token; }
bool i2c_manager_probe(uint8_t, int) { return true; }
bool i2c_manager_write(uint8_t, const uint8_t*, size_t, int) { return true; }
bool i2c_manager_read(uint8_t, uint8_t*, size_t, int) { return true; }
bool i2c_manager_write_read(uint8_t, const uint8_t*, size_t, uint8_t*, size_t, int) { return true; }
bool i2c_manager_read_register(uint8_t, uint8_t, uint8_t*, size_t, int) { return true; }
