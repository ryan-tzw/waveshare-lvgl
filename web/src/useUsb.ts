import { fromBinary } from "@bufbuild/protobuf";
import { useRef, useState } from "react";

import { type NetworkPacket, NetworkPacketSchema } from "./generated/protocol_pb";

const isWebUsbSupported = "usb" in navigator;
const usbVendorId = 0xcafe;
const configurationValue = 1;
const vendorInterfaceNumber = 2;
const vendorEndpointNumber = 3;
const vendorEndpointBufferSize = 64;
const setControlLineStateRequest = 0x22;
const maximumMessageLength = 240;
const messageLengthSize = 2;

function handleWebUsbMessage(message: Uint8Array, onPacket: (packet: NetworkPacket) => void) {
    try {
        const packet = fromBinary(NetworkPacketSchema, message);
        onPacket(packet);
    } catch (error) {
        console.error("Could not decode WebUSB message:", error);
    }
}

export function useUsb(onPacket: (packet: NetworkPacket) => void) {
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
        let receiveBuffer = new Uint8Array();

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
                    const receivedBytes = new Uint8Array(
                        result.data.buffer,
                        result.data.byteOffset,
                        result.data.byteLength,
                    );
                    const combinedBuffer = new Uint8Array(
                        receiveBuffer.length + receivedBytes.length,
                    );

                    combinedBuffer.set(receiveBuffer);
                    combinedBuffer.set(receivedBytes, receiveBuffer.length);
                    receiveBuffer = combinedBuffer;

                    while (receiveBuffer.length >= messageLengthSize) {
                        const lowByte = receiveBuffer[0];
                        const highByte = receiveBuffer[1];
                        const messageLength = (highByte << 8) | lowByte;

                        if (messageLength > maximumMessageLength) {
                            throw new Error(`WebUSB message is too large: ${messageLength} bytes`);
                        }

                        const frameLength = messageLengthSize + messageLength;
                        if (receiveBuffer.length < frameLength) break;

                        const message = receiveBuffer.slice(messageLengthSize, frameLength);
                        handleWebUsbMessage(message, onPacket);

                        receiveBuffer = receiveBuffer.slice(frameLength);
                    }
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
