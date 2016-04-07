#define USE_ASYNC_IO

#define kOurVendorID 0x0582
#define kOurProductID 0x00ad
#define kOurProductIDBulkTest   4098 

#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <stdio.h>
#include <mach/mach.h>

#include <CoreFoundation/CoreFoundation.h>

//Global variables
static IONotificationPortRef    gNotifyPort;
static io_iterator_t            gRawAddedIter;
static io_iterator_t            gRawRemovedIter;
static io_iterator_t            gBulkTestAddedIter;
static io_iterator_t            gBulkTestRemovedIter;
static char                     gBuffer[64];

IOReturn ConfigureDevice(IOUSBDeviceInterface **dev)
{
    UInt8                           numConfig;
    IOReturn                        kr;
    IOUSBConfigurationDescriptorPtr configDesc;
 
    //Get the number of configurations. The sample code always chooses
    //the first configuration (at index 0) but your code may need a
    //different one
    kr = (*dev)->GetNumberOfConfigurations(dev, &numConfig);
    if (!numConfig)
        return -1;
 
    //Get the configuration descriptor for index 0
    kr = (*dev)->GetConfigurationDescriptorPtr(dev, 0, &configDesc);
    if (kr)
    {
        printf("Couldn’t get configuration descriptor for index %d (err = %08x)\n", 0, kr);
        return -1;
    }
 
    //Set the device’s configuration. The configuration value is found in
    //the bConfigurationValue field of the configuration descriptor
    kr = (*dev)->SetConfiguration(dev, configDesc->bConfigurationValue);
    if (kr)
    {
        printf("Couldn’t set configuration to value %d (err = %08x)\n", 0,
                kr);
        return -1;
    }
    return kIOReturnSuccess;
}

void RawDeviceAdded(void *refCon, io_iterator_t iterator)
{
    kern_return_t               kr;
    io_service_t                usbDevice;
    IOCFPlugInInterface         **plugInInterface = NULL;
    IOUSBDeviceInterface        **dev = NULL;
    HRESULT                     result;
    SInt32                      score;
    UInt16                      vendor;
    UInt16                      product;
    UInt16                      release;
 
    while (usbDevice = IOIteratorNext(iterator))
    {
        //Create an intermediate plug-in
        kr = IOCreatePlugInInterfaceForService(usbDevice,
                    kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID,
                    &plugInInterface, &score);
        //Don’t need the device object after intermediate plug-in is created
        kr = IOObjectRelease(usbDevice);
        if ((kIOReturnSuccess != kr) || !plugInInterface)
        {
            printf("Unable to create a plug-in (%08x)\n", kr);
            continue;
        }
        //Now create the device interface
        result = (*plugInInterface)->QueryInterface(plugInInterface,
                        CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID),
                        (LPVOID *)&dev);
        //Don’t need the intermediate plug-in after device interface
        //is created
        (*plugInInterface)->Release(plugInInterface);
 
        if (result || !dev)
        {
            printf("Couldn’t create a device interface (%08x)\n",
                                                    (int) result);
            continue;
        }
 
        //Check these values for confirmation
        kr = (*dev)->GetDeviceVendor(dev, &vendor);
        kr = (*dev)->GetDeviceProduct(dev, &product);
        kr = (*dev)->GetDeviceReleaseNumber(dev, &release);
        if ((vendor != kOurVendorID) || (product != kOurProductID) ||
            (release != 1))
        {
            printf("Found unwanted device (vendor = %d, product = %d)\n",
                    vendor, product);
            printf("(or the release was not equal to 1: release = %d)\n", release);
            (void) (*dev)->Release(dev);
            continue;
        }
 
        //Open the device to change its state
        kr = (*dev)->USBDeviceOpen(dev);
        if (kr != kIOReturnSuccess)
        {
            printf("Unable to open device: %08x\n", kr);
            (void) (*dev)->Release(dev);
            continue;
        }
        //Configure device
        kr = ConfigureDevice(dev);
        if (kr != kIOReturnSuccess)
        {
            printf("Unable to configure device: %08x\n", kr);
            (void) (*dev)->USBDeviceClose(dev);
            (void) (*dev)->Release(dev);
            continue;
        }
/* 
        //Download firmware to device
        kr = DownloadToDevice(dev);
        if (kr != kIOReturnSuccess)
        {
            printf("Unable to download firmware to device: %08x\n", kr);
            (void) (*dev)->USBDeviceClose(dev);
            (void) (*dev)->Release(dev);
            continue;
        }
 */
        //Close this device and release object
        kr = (*dev)->USBDeviceClose(dev);
        kr = (*dev)->Release(dev);
    }
}


int main (int argc, const char *argv[])
{
    mach_port_t                 masterPort;
    CFMutableDictionaryRef      matchingDict;
    CFRunLoopSourceRef          runLoopSource;
    kern_return_t               kr;
    SInt32                      usbVendor = kOurVendorID;
    SInt32                      usbProduct = kOurProductID;
    
    kr = IOMasterPort(MACH_PORT_NULL, &masterPort);

    if (kr || !masterPort)
    {
        printf("ERR: Couldn't create a master I/O Kit port(%08x)\n", kr);
        return -1;
    }

    // Setup matching dictionary for class IOUSBDevice and its subclasses

    matchingDict = IOServiceMatching(kIOUSBDeviceClassName);

    if (!matchingDict)
    {

        printf("couldn't craete a usb matching dictionary :(\n");

        mach_port_deallocate(mach_task_self(), masterPort);

        return -1;
    }

    // add vendor and product Ids to dict

    CFDictionarySetValue(matchingDict, CFSTR(kUSBVendorName),
                            CFNumberCreate(kCFAllocatorDefault,
                                        kCFNumberSInt32Type, &usbVendor));

    CFDictionarySetValue(matchingDict, CFSTR(kUSBProductName),
                            CFNumberCreate(kCFAllocatorDefault,
                                        kCFNumberSInt32Type, &usbProduct));

    gNotifyPort = IONotificationPortCreate(masterPort);
    runLoopSource = IONotificationPortGetRunLoopSource(gNotifyPort);
    CFRunLoopAddSource(CFRunLoopGetCurrent(), runLoopSource,
                    kCFRunLoopDefaultMode);

    //Now set up two notifications: one to be called when a raw device
    //is first matched by the I/O Kit and another to be called when the
    //device is terminated
    //Notification of first match:
    kr = IOServiceAddMatchingNotification(gNotifyPort,
                    kIOFirstMatchNotification, matchingDict,
                    RawDeviceAdded, NULL, &gRawAddedIter);
    //Iterate over set of matching devices to access already-present devices
    //and to arm the notification
    RawDeviceAdded(NULL, gRawAddedIter);


/* 
    //Notification of termination:
    kr = IOServiceAddMatchingNotification(gNotifyPort,
                    kIOTerminatedNotification, matchingDict,
                    RawDeviceRemoved, NULL, &gRawRemovedIter);
    //Iterate over set of matching devices to release each one and to
    //arm the notification
    RawDeviceRemoved(NULL, gRawRemovedIter);
*/
    mach_port_deallocate(mach_task_self(), masterPort);
    masterPort = 0;

    CFRunLoopRun();

    return 0;
}
