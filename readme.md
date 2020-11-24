# Driver for DS18B20 in RPI

Driver DS18B20 (1wire temperature sensors) for GPIO Raspberry pi. Sensors dialogue through the primitives GPIO. It is operational for several devices and it can read the temperature and change the resolution for each device.

```
read kernel returns :
dmesg -wH &

Compile and Init (udev) :
make
sudo insmod driver.ko my_gpio=<INT_GPIO> 

Read temperature :
cat /dev/myDevice/device_DS18B20_<MINOR>

Change resolution (9 to 12) :
sudo echo '[9-12]' > /dev/myDevice/device_DS18B20_<MINOR>

Exit :
sudo rmmod driver
```