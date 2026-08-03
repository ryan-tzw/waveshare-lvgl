import { useRef, useState } from "react";

const isWebUsbSupported = "usb" in navigator;
const usbVendorId = 0xcafe;
const configurationValue = 1;
const vendorInterfaceNumber = 2;
const vendorEndpointNumber = 3;
const vendorEndpointBufferSize = 64;
const setControlLineStateRequest = 0x22;

export function useUsb() {
    const deviceRef = useRef<USBDevice | null>(null);
    const readPromiseRef = useRef<Promise<void> | null>(null);
    const keepReadingRef = useRef(false);

    const [connected, setConnected] = useState(false);

    async function connect() {
        if (!isWebUsbSupported) {
            console.error("connect(): WebUSB unavailable");
            return;
        }

        let device: USBDevice | null = null;

        try {
            device = await navigator.usb.requestDevice({
                filters: [{ vendorId: usbVendorId }],
            });

            await device.open();

            if (!device.configuration) {
                await device.selectConfiguration(configurationValue);
            }

            await device.claimInterface(vendorInterfaceNumber);

            const connectionResult = await device.controlTransferOut({
                requestType: "class",
                recipient: "interface",
                request: setControlLineStateRequest,
                value: 1,
                index: vendorInterfaceNumber,
            });

            if (connectionResult.status !== "ok") {
                throw new Error("Could not notify the device of the WebUSB connection");
            }

            deviceRef.current = device;
            keepReadingRef.current = true;
            setConnected(true);

            readPromiseRef.current = readFromDevice(device);
        } catch (error) {
            console.error(error);

            if (device?.opened) {
                await device.close();
            }
        }
    }

    async function readFromDevice(device: USBDevice) {
        const decoder = new TextDecoder();

        try {
            while (keepReadingRef.current) {
                const result = await device.transferIn(
                    vendorEndpointNumber,
                    vendorEndpointBufferSize,
                );

                if (result.status !== "ok") {
                    throw new Error(`WebUSB read failed with status: ${result.status}`);
                }

                if (result.data) {
                    console.log(decoder.decode(result.data));
                }
            }
        } catch (error) {
            if (keepReadingRef.current) {
                console.error(error);
            }
        }
    }

    async function disconnect() {
        const device = deviceRef.current;

        if (!device) {
            console.error("disconnect(): WebUSB device unavailable");
            return;
        }

        keepReadingRef.current = false;

        try {
            const disconnectionResult = await device.controlTransferOut({
                requestType: "class",
                recipient: "interface",
                request: setControlLineStateRequest,
                value: 0,
                index: vendorInterfaceNumber,
            });

            if (disconnectionResult.status !== "ok") {
                console.error("Could not notify the device of the WebUSB disconnection");
            }
        } catch (error) {
            console.error(error);
        }

        await device.close();
        await readPromiseRef.current;

        deviceRef.current = null;
        readPromiseRef.current = null;
        setConnected(false);
    }

    return {
        connected,
        connect,
        disconnect,
    };
}
