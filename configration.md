[env:nodemcuv2]
platform = espressif8266
board = nodemcuv2          ; or d1_mini if you use Wemos D1 mini
framework = arduino
upload_speed = 921600
monitor_speed = 115200
lib_deps =
  adafruit/DHT sensor library @ ^1.4.6
  paulstoffregen/OneWire @ ^2.3.8
  milesburton/DallasTemperature @ ^3.11.0
  adafruit/Adafruit BMP280 Library @ ^2.6.8
  adafruit/Adafruit Unified Sensor @ ^1.1.14
