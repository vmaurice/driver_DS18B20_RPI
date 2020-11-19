/*
Nom : Maurice
Prenom : Vincent
*/


/*
 * Commands
 * 
 * read return of kernel :
 * dmesg -wH &
 * 
 * Compile and Init (udev) :
 * make
 * sudo insmod driver.ko my_gpio=<INT_GPIO> 
 * 
 * Read temperature :
 * cat /dev/myDevice/device_DS18B20_<MINOR>
 * 
 * Change resolution (9 to 12) :
 * sudo echo '[9-12]' > /dev/myDevice/device_DS18B20_<MINOR>
 * 
 * Exit :
 * sudo rmmod driver
 * 
 */

/* Includes */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/cdev.h>
#include <linux/kdev_t.h>
#include <linux/device.h>

#include <linux/gpio.h> 
#include <linux/delay.h>

#include <linux/list.h>


/* Table for CRC-7 (polynomial x^7 + x^3 + 1) */
const u8 crc7_syndrome_table[256] = {
	0, 94, 188, 226, 97, 63, 221, 131, 194, 156, 126, 32, 163, 253, 31, 65,
	157, 195, 33, 127, 252, 162, 64, 30, 95, 1, 227, 189, 62, 96, 130, 220,
	35, 125, 159, 193, 66, 28, 254, 160, 225, 191, 93, 3, 128, 222, 60, 98,
	190, 224, 2, 92, 223, 129, 99, 61, 124, 34, 192, 158, 29, 67, 161, 255,
	70, 24, 250, 164, 39, 121, 155, 197, 132, 218, 56, 102, 229, 187, 89, 7,
	219, 133, 103, 57, 186, 228, 6, 88, 25, 71, 165, 251, 120, 38, 196, 154,
	101, 59, 217, 135, 4, 90, 184, 230, 167, 249, 27, 69, 198, 152, 122, 36,
	248, 166, 68, 26, 153, 199, 37, 123, 58, 100, 134, 216, 91, 5, 231, 185,
	140, 210, 48, 110, 237, 179, 81, 15, 78, 16, 242, 172, 47, 113, 147, 205,
	17, 79, 173, 243, 112, 46, 204, 146, 211, 141, 111, 49, 178, 236, 14, 80,
	175, 241, 19, 77, 206, 144, 114, 44, 109, 51, 209, 143, 12, 82, 176, 238,
	50, 108, 142, 208, 83, 13, 239, 177, 240, 174, 76, 18, 145, 207, 45, 115,
	202, 148, 118, 40, 171, 245, 23, 73, 8, 86, 180, 234, 105, 55, 213, 139,
	87, 9, 235, 181, 54, 104, 138, 212, 149, 203, 41, 119, 244, 170, 72, 22,
	233, 183, 85, 11, 136, 214, 52, 106, 43, 117, 151, 201, 74, 20, 246, 168,
	116, 42, 200, 150, 21, 75, 169, 247, 182, 232, 10, 84, 215, 137, 107, 53
};


#define MY_DEVICE      "mydevice"
#define GPIO_NUMBER     4 // By default, bus wire in rpi

// gpio define
int my_gpio = GPIO_NUMBER;
module_param(my_gpio,int,S_IRUGO);

#define noDEBUG // if DEBUG is define => print of bits received
#define DELAY_READ 60 // Delay read and write for 1wire
#define DELAY_ERR 500 // Wait before the new demand when the data is corrupted 
#define MAX_REPEAT_ERR 5 // Max repeat when error

int my_resolution = 12; // resolution by default

u64 device; // rom of device
int my_minor = 0; // minor currently
int nbDevice = 0; // number of device


// Structure of link list for each device
struct maStructure{
    int minor;
    u64 device;
    struct list_head liste;
};

// Init
static struct maStructure maListe;

struct maStructure *tmpListe;
struct list_head *pos, *q;

// Mutex
struct mutex lock;

// UDEV
static struct class *myClass;


// Counts errors when searching for device
int errSearch = 0; 






// Char driver functions 
static ssize_t gpio_read(struct file *f, char *buf, size_t size, loff_t *offset);
static ssize_t gpio_write(struct file *f, const char *buf, size_t size, loff_t *offset);
static int gpio_open(struct inode *in, struct file *f);
static int gpio_release(struct inode *in, struct file *f);

// Find the sensors
static int search(void);

// Send one byte
static int send(unsigned char n);
// Send 8 bytes
static int sendRom(u64 device);

// Read n bit(s)
static int read(int n);

// Reset of 1wire
static void reset(void);

// standard file_ops for char driver 
static struct file_operations fops = 
{
  .read = gpio_read,
  .write = gpio_write,
  .open = gpio_open,
  .release = gpio_release,
};


// The dev_t for our device
dev_t dev;

// The cdev for our device
struct cdev *my_cdev;


// read of temperature (DS18B20)
static ssize_t gpio_read(struct file *f, char *buf, size_t size, loff_t *offset) {
    // get bytes to read temperature
    u8 upper;
    u8 lower;
    u8 resolution;

    // check data
    u8 crc;
    u8 tmp;

    // tempory data
    int i;
    int val;

    // temperature
    int temp;
    int temp_float;

    int err;
     
    printk(KERN_INFO "mydevice : >>> GPIO READ called\n");


    // reset
    reset();
    
    // Send Ox55 (chose sensor)
    send(0x55);
    
    sendRom(device);
    
    
    // send 0x44 (conv temperature)
    send(0x44);

    // Wait 1
    gpio_direction_output(my_gpio, 0);
    udelay(1);        
    gpio_direction_input(my_gpio);
    udelay(10);

    while (gpio_get_value(my_gpio) == 0) {
        udelay(DELAY_READ);
        gpio_direction_output(my_gpio, 0);
        udelay(1);
        gpio_direction_input(my_gpio);
        udelay(10);
    }


    udelay(DELAY_READ);

    // Add delay for conversion
    if (my_resolution == 9)
        mdelay(150);
    else if (my_resolution == 10)
        mdelay(200);
    else if (my_resolution == 11)
        mdelay(400);
    else
        mdelay(800);
    
    
    upper = 0;
    lower = 0;
    resolution = 0;

    crc = 0x00;
    tmp = 0xff;
    
    err = 0;
    
    // repeat MAX_REPEAT_ERR times when crc is bad
    while (crc != tmp && err < MAX_REPEAT_ERR) {

        crc = 0x00;
        tmp = 0x00;
    
        // Reset
        reset();


        // Send Ox55 (chose sensor)
        send(0x55);
        
        sendRom(device);
        
        
        // send 0xBE  10111110
        // Receive rom of device

        send(0xBE);

        printk(KERN_INFO "mydevice : read 64 bits\n");


        for (i = 0; i < 72; i++) {
            #ifdef DEBUG
                if (i%8 == 0) {
                    printk(KERN_INFO "\n");
                    printk(KERN_INFO "mydevice : value : ");
                }
            #endif
            gpio_direction_output(my_gpio, 0);
            udelay(1);
            gpio_direction_input(my_gpio);
            udelay(5);
            val = gpio_get_value(my_gpio);
            #ifdef DEBUG
                printk(KERN_INFO "%i ", val);
            #endif
            if (i < 8)
            upper += val << i;
            if (i >= 8 && i < 16)
                lower += val << (i % 8);
            if (i >= 32 && i < 40)
                resolution += val << (i % 32);
            udelay(DELAY_READ);
            
            gpio_direction_input(my_gpio);
            while (gpio_get_value(my_gpio) != 1);

            

            if (i%8 == 0 && i > 0 &&  i <= 64) {
                //printk(KERN_INFO "tmp 0x%x\n", tmp);
                crc ^= tmp;
                //printk(KERN_INFO "crc before %i :  0x%x\n", (int)crc, crc);
                crc = crc7_syndrome_table[(int)crc];
                //printk(KERN_INFO "crc after 0x%x\n", crc);
                tmp = 0;
            }

            if (i < 64)
                tmp += val << (i % 8);

            if (i == 64)
                tmp = 0;
            if (i >= 64) {
                tmp += val << (i % 8);
            }
        }

        if (crc == tmp)
            printk(KERN_INFO "mydevice : CRC ok\n");
        else {
            printk(KERN_ERR "mydevice : CRC ko\n");
            err++;
            mdelay(DELAY_ERR);
        }
    }

    if (err == 5)
        return -EBADE;
            

    // Calculate the resolution

    if (resolution == 0b00011111)
        my_resolution = 9;
    else if (resolution == 0b00111111)
        my_resolution = 10;
    else if (resolution == 0b01011111)
        my_resolution = 11;
    else if (resolution == 0b01111111)
        my_resolution = 12;
    else 
    {
        printk(KERN_ERR "mydevice : error resoltion\n");
        return -1;
    }
    
    printk(KERN_INFO "mydevice : resolution : %i\n", my_resolution);


    // Calculate the temperature

    printk(KERN_INFO "mydevice : upper : 0x%x\n", upper);
    printk(KERN_INFO "mydevice : lower : 0x%x\n", lower);

    temp = 0;
    temp_float = 0;
    
    if (my_resolution > 11 && upper & 0x01)
        temp_float += 63;

    if (my_resolution > 10 && upper & 0x02)
        temp_float += 125;

    if (my_resolution > 9 && upper & 0x04)
        temp_float += 250;

    if (upper & 0x08)
        temp_float += 500;
    

    if (upper & 0x10)
        temp += 1;

    if (upper & 0x20)
        temp += 2;

    if (upper & 0x40)
        temp += 4;
        
    if (upper & 0x80)
        temp += 8;
    

    if (lower & 0x01)
        temp += 16;

    if (lower & 0x02)
        temp += 32;

    if (lower & 0x04)
        temp += 64;


    if (lower & 0xf0)
        temp *= -1;


    if (temp_float == 63)
        printk(KERN_INFO "mydevice : temperature : %i.0%i °C\n", temp, temp_float);
    else
        printk(KERN_INFO "mydevice : temperature : %i.%i °C\n", temp, temp_float);

    
    
   
    
    return 0;
}

// Change resolution of DS18B20
static ssize_t gpio_write(struct file *f, const char *buf, size_t size, loff_t *offset) {
    int err, err2;
    int i;
    int val;

    u8 resolution;

    u8 crc;
    u8 tmp;

    printk(KERN_INFO "mydevice : >>> GPIO WRITE called\n");

    if ( (err = kstrtoint(buf,10, &my_resolution)) )
        printk(KERN_ERR "mydevice : Error conversion : %i\n", err);

    printk(KERN_INFO "mydevice : value %i\n", my_resolution);


    err = 0;

    while (err < MAX_REPEAT_ERR) {

        // reset

        reset();

        // Send Ox55 (chose sensor)
        send(0x55);
        
        sendRom(device);
        

        // send 0x4E

        send(0x4E);

        // send 0x00 0x00 

        send(0x00);
        send(0x00);

        // send resolution

        if (my_resolution == 9)
            send(0b00011111);
        else if (my_resolution == 10)
            send(0b00111111);
        else if (my_resolution == 11)
            send(0b01011111);
        else if (my_resolution == 12)
            send(0b01111111);
        else
            printk(KERN_ERR "mydevice : Error value\n");

        
        mdelay(2000);


        // check up

        crc = 0x00;
        tmp = 0xff;
        err2 = 0;

        while (crc != tmp && err2 < MAX_REPEAT_ERR) {

            crc = 0x00;
            tmp = 0x00;

            reset();
            // Send Ox55 (chose sensor)
            
            send(0x55);
            sendRom(device);
            
            
            // send 0xBE  10111110
            // Receive rom of device
            send(0xBE);

            for (i=0;i<72;i++) {
                #ifdef DEBUG
                    if (i%8 == 0) {
                        printk(KERN_INFO "\n");
                        printk(KERN_INFO "mydevice : value : ");
                    }
                #endif

                gpio_direction_output(my_gpio, 0);
                udelay(1);
                gpio_direction_input(my_gpio);
                udelay(5);
                val = gpio_get_value(my_gpio);

                #ifdef DEBUG
                    printk(KERN_INFO "%i ", val);
                #endif

                if (i >= 32 && i < 40)
                    resolution += val << (i % 32);
                udelay(DELAY_READ);
                
                gpio_direction_input(my_gpio);
                while (gpio_get_value(my_gpio) != 1);

                if (i%8 == 0 && i > 0 &&  i <= 64) {
                    //printk(KERN_INFO "tmp 0x%x\n", tmp);
                    crc ^= tmp;
                    //printk(KERN_INFO "crc before %i :  0x%x\n", (int)crc, crc);
                    crc = crc7_syndrome_table[(int)crc];
                    //printk(KERN_INFO "crc after 0x%x\n", crc);
                    tmp = 0;
                }

                if (i < 64)
                    tmp += val << (i % 8);

                if (i == 64)
                    tmp = 0;
                if (i >= 64) {
                    tmp += val << (i % 8);
                }
            }
            
            if (crc == tmp)
                printk(KERN_INFO "mydevice : CRC ok\n");
            else {
                printk(KERN_ERR "mydevice : CRC ko\n");
                err2++;
                mdelay(DELAY_ERR);
            }

        }

        if (err2 == 5)
            return -EBADE;

        if (resolution == 0b00011111 && my_resolution == 9) {
            printk(KERN_INFO "mydevice : resolution ok \n");
            return size;
        }  
        else if (resolution == 0b00111111 && my_resolution == 10) {
            printk(KERN_INFO "mydevice : resolution ok \n");
            return size;
        }
        else if (resolution == 0b01011111 && my_resolution == 11) {
            printk(KERN_INFO "mydevice : resolution ok \n");
            return size;
        }    
        else if (resolution == 0b01111111 && my_resolution == 12) {
            printk(KERN_INFO "mydevice : resolution ok \n");
            return size;
        }
        
        printk(KERN_ERR "mydevice : resolution ko \n");
        err++;

        mdelay(DELAY_ERR);
    }

    return -ECOMM;
}

// When open device
static int gpio_open(struct inode *in, struct file *f) {
    
    printk(KERN_INFO "mydevice : >>> GPIO OPEN called\n");

    mutex_lock(&lock);

    if (MINOR(in->i_rdev) != my_minor || device == 0) {
        my_minor = MINOR(in->i_rdev);
        list_for_each_safe(pos, q, &maListe.liste){
            tmpListe = list_entry(pos, struct maStructure, liste);
            if (tmpListe->minor == my_minor) {
                device = tmpListe->device;
            }
        }
    }

    return 0;
}

// When close device
static int gpio_release(struct inode *in, struct file *f) {
    printk(KERN_INFO "mydevice : >>> GPIO RELEASE called\n");

    mutex_unlock(&lock);

    return 0;
}


// Send one byte
static int send(unsigned char n) {
    int i;
    unsigned char c = 1;

    printk(KERN_INFO "mydevice : send 0x%x\n", n);

    for (i = 0; i < 8; i++) {
        if (n & (c << i)) {
            gpio_direction_output(my_gpio, 0);
            udelay(15);
            gpio_direction_input(my_gpio);
            udelay(45);
            gpio_direction_input(my_gpio);
            //printk(KERN_INFO "1");
        } else {
            gpio_direction_output(my_gpio, 0);
            udelay(60);
            gpio_direction_input(my_gpio);
            //printk(KERN_INFO "0");
        }
    }

    return 0;
}

// Send 8 bytes (for rom device)
static int sendRom(u64 device) {
    int i;
    printk(KERN_INFO "mydevice : send Rom\n");

    for (i=0; i<64;i++) {
        if (device & ((u64)1 << i)) {
            gpio_direction_output(my_gpio, 0);
            udelay(15);
            gpio_direction_input(my_gpio);
            udelay(45);
            gpio_direction_input(my_gpio);
            //printk(KERN_INFO "mydevice : 1\n");
        } else {
            gpio_direction_output(my_gpio, 0);
            udelay(60);
            gpio_direction_input(my_gpio);
            //printk(KERN_INFO "mydevice : 0\n");
        }
    }

    return 0;
}

// Read n bit(s)
static int read(int n) {
    int i, val;
    
    printk(KERN_INFO "mydevice : read %i bits\n", n);
    
    for (i = 0; i < n; i++) {
        #ifdef DEBUG
            if (i%8 == 0) {
                printk(KERN_INFO "\n");
                printk(KERN_INFO "mydevice : value : ");
            }
        #endif
        gpio_direction_output(my_gpio, 0);
        udelay(1);
        gpio_direction_input(my_gpio);
        udelay(5);
        val = gpio_get_value(my_gpio);
        #ifdef DEBUG
            printk(KERN_INFO "%i ", val);
        #endif
        udelay(DELAY_READ);

        gpio_direction_input(my_gpio);
        while (gpio_get_value(my_gpio) != 1);
    }
    return 0;
}

// Reset of 1wire
static void reset(void) {
    printk(KERN_INFO "mydevice : reset 1wire called\n");

    gpio_direction_output(my_gpio, 0);
    udelay(480);
    gpio_direction_input(my_gpio);
    udelay(480);
}

// Search all device (DS18B20)
// @return number of device
static int search(void) {
    int i = 0; // For loop
    int r = 0; // result of the response of slave 
    int n = 0; // start where the conflict
    int b = 0; // save place of first conflict
    int nbDevice = 0;
    u8 family = 0x28; // number of family at DS18B20

    u8 crc, tmp;

    printk(KERN_INFO "mydevice : search device (DS18B20)\n");

    // DS18B20, 0x28 called

    device = 0;

    device += family;

    b = 0;
    n = 8;

    while (true) {

        crc = 0x00;
        tmp = 0x00;

        reset();
        send(0xF0);

        for (i = 0; i < n; i++) {

            if (i%8 == 0 && i > 0 &&  i <= 56) {
                //printk(KERN_INFO "tmp 0x%x\n", tmp);
                crc ^= tmp;
                //printk(KERN_INFO "crc before %i :  0x%x\n", (int)crc, crc);
                crc = crc7_syndrome_table[(int)crc];
                //printk(KERN_INFO "crc after 0x%x\n", crc);
                tmp = 0;
            }

            gpio_direction_output(my_gpio, 0);
            udelay(1);        
            gpio_direction_input(my_gpio);
            udelay(10);
            udelay(DELAY_READ);

            while (gpio_get_value(my_gpio) != 1);

            gpio_direction_output(my_gpio, 0);
            udelay(1);        
            gpio_direction_input(my_gpio);
            udelay(10);
            udelay(DELAY_READ);

            while (gpio_get_value(my_gpio) != 1);


            if (device & ((u64)1 << i)) {
                //write 1
                gpio_direction_output(my_gpio, 0);
                udelay(15);
                gpio_direction_input(my_gpio);
                udelay(45);
                gpio_direction_input(my_gpio);
                //printk(KERN_INFO "mydevice : 1\n");
                
                tmp += 1 << (i % 8);
            } else {
                // write 0
                gpio_direction_output(my_gpio, 0);
                udelay(60);
                gpio_direction_input(my_gpio);
                //printk(KERN_INFO "mydevice : 0\n");
            }
        }

        for (i=n;i<64;i++) {
            if (device & ((u64)1 << i))
                device += (u64)1 << i;

            r = 0;

            if (i%8 == 0 && i > 0 &&  i <= 56) {
                //printk(KERN_INFO "tmp 0x%x\n", tmp);
                crc ^= tmp;
                //printk(KERN_INFO "crc before %i :  0x%x\n", (int)crc, crc);
                crc = crc7_syndrome_table[(int)crc];
                //printk(KERN_INFO "crc after 0x%x\n", crc);
                tmp = 0;
            }

            gpio_direction_output(my_gpio, 0);
            udelay(1);        
            gpio_direction_input(my_gpio);
            udelay(10);
            if (gpio_get_value(my_gpio) == 0)
                r += 1;
            udelay(DELAY_READ);

            while (gpio_get_value(my_gpio) != 1);

            gpio_direction_output(my_gpio, 0);
            udelay(1);        
            gpio_direction_input(my_gpio);
            udelay(10);
            if (gpio_get_value(my_gpio) == 0)
                r += 2;
            udelay(DELAY_READ);

            while (gpio_get_value(my_gpio) != 1);

            //printk(KERN_INFO "mydevice : %i\n", r);

            if ((r == 1 || r == 3) && b != i) {
                device += (u64)0 << i;
                // write 0
                gpio_direction_output(my_gpio, 0);
                udelay(60);
                gpio_direction_input(my_gpio);
                if (r == 3 && b == 0)
                    b = i; 
            } else if (r == 2 || b == i) {
                device += (u64)1 << i;
                //write 1
                gpio_direction_output(my_gpio, 0);
                udelay(15);
                gpio_direction_input(my_gpio);
                udelay(45);
                gpio_direction_input(my_gpio);
                if (b == i)
                    b = 0;

                tmp += 1 << (i % 8);
            } else {
                printk(KERN_ERR "mydevice : device %i, no device (11) : %i\n", nbDevice, i);

                list_for_each_safe(pos, q, &maListe.liste){
                    tmpListe = list_entry(pos, struct maStructure, liste);
                    list_del(pos);
                    kfree(tmpListe);
                }

                if (errSearch < MAX_REPEAT_ERR) {
                    errSearch++;
                    printk(KERN_INFO "mydevice : restart search rom\n");
                    mdelay(DELAY_ERR);
                    return search();
                } else {
                    printk(KERN_INFO "mydevice : cannot search rom\n");
                    return 0;
                }
            }
        }

        //printk(KERN_INFO "mydevice : crc 0x%x\n", crc);
        //printk(KERN_INFO "mydevice : tmp 0x%x\n", tmp);

        if (crc != tmp) {
            printk(KERN_ERR "mydevice : device %i, CRC KO\n", nbDevice);

            list_for_each_safe(pos, q, &maListe.liste){
                tmpListe = list_entry(pos, struct maStructure, liste);
                list_del(pos);
                kfree(tmpListe);
            }

            if (errSearch < MAX_REPEAT_ERR) {
                errSearch++;
                printk(KERN_INFO "mydevice : restart search rom\n");
                mdelay(DELAY_ERR);
                return search();
            } else {
                errSearch = 0;
                printk(KERN_INFO "mydevice : cannot search rom\n");
                return 0;
            }
        } else {
            printk(KERN_INFO "mydevice : device %i, CRC OK\n", nbDevice);
        }

        tmpListe = (struct maStructure *)kmalloc(sizeof(struct maStructure), GFP_KERNEL);
        tmpListe->minor = nbDevice;
        tmpListe->device = device;

        // Add at the end of the list
        list_add_tail(&tmpListe->liste, &maListe.liste);


        nbDevice++;

        n = b;

        if (b == 0) {
            errSearch = 0;
            return nbDevice;
        }
            
    }
    errSearch = 0;
    return nbDevice;
}

// Put the autorisations
static char *mydevnode(struct device *dev, umode_t *mode)
{
    if (mode)
        *mode = 0666; 
    return NULL;
}



// Init the char device
int gpio_init(void)
{
    int i = 0;

    nbDevice = 0;

    printk(KERN_INFO "mydevice : >>> GPIO INIT called\n");
    printk(KERN_INFO "mydevice : my_gpio : %i\n",my_gpio);

    // Initialisation 
    INIT_LIST_HEAD(&maListe.liste);

    nbDevice = search();

    if (nbDevice == 0) {
        printk(KERN_ALERT "mydevice : any device\n");
        return -ENODEV;
    }
        

    printk(KERN_INFO "mydevice : >>> nb device : %i \n", nbDevice);


	// Dynamic allocation for (major,minor)
	if (alloc_chrdev_region(&dev,0,nbDevice,"device_DS18B20_0") == -1)
	{
		printk(KERN_ALERT "mydevice : >>> ERROR alloc_chrdev_region\n");
		return -EINVAL;
	}
	// Print out the values
	//printk(KERN_INFO "mydevice : init allocated (major, minor)=(%d,%d)\n",MAJOR(dev),0);

    for(i=1;i<nbDevice;i++) {
        if (register_chrdev_region(MAJOR(dev), i, "device_DS18B20_" + (char)i) == -1)
        {
            printk(KERN_ALERT ">>> ERROR alloc_chrdev_region\n");
            return -EINVAL;
        }
        // Print out the values
	    //printk(KERN_INFO "mydevice : init allocated (major, minor)=(%d,%d)\n",MAJOR(dev),i);
    }

	// Structures allocation
	my_cdev = cdev_alloc();
	my_cdev->ops = &fops;
	my_cdev->owner = THIS_MODULE;
	// linking operations to device
	cdev_add(my_cdev,dev,nbDevice);

    /* Create peripheral class */
    myClass = class_create(THIS_MODULE, "myDevice");
    myClass->devnode = mydevnode;

    for (i=0;i<nbDevice;i++) {
        device_create(myClass, NULL, MKDEV(MAJOR(dev), i), NULL, "/myDevice/device_DS18B20_%i", i);
        printk(KERN_INFO "mydevice : device %i in /dev/myDevice/device_DS18B20_%i\n", i, i);
    }
    

    if (!gpio_is_valid(my_gpio)){
        printk(KERN_ERR "mydevice: invalid GPIO\n");
        return -ENODEV;
    }

    if (gpio_request(my_gpio, MY_DEVICE) < 0) {
        printk(KERN_ALERT "mydevice : error gpio_request\n");
        return -1;
    }

    printk(KERN_INFO "mydevice : >>> end GPIO EXIT called\n");

	return(0);


}


// Exit the char device
static void gpio_cleanup(void)
{
    int i;

    printk(KERN_INFO "mydevice : >>> GPIO EXIT called\n");

    // free list
    list_for_each_safe(pos, q, &maListe.liste){
        tmpListe = list_entry(pos, struct maStructure, liste);
        list_del(pos);
        kfree(tmpListe);
    }

    //gpio_unexport(my_gpio);
    gpio_free(my_gpio);

	// Unregister
	unregister_chrdev_region(dev,nbDevice);

    // node deleted
    for (i = 0; i < nbDevice; i++) {
        device_destroy(myClass, MKDEV(MAJOR(dev), i));
    }

    // class deleted
    class_destroy(myClass);

	// cdev deleted
	cdev_del(my_cdev);
}

module_exit(gpio_cleanup);
module_init(gpio_init);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vincent Maurice");
MODULE_DESCRIPTION("Driver GPIO for DS18B20 sensors");
MODULE_SUPPORTED_DEVICE("MyDevice");
