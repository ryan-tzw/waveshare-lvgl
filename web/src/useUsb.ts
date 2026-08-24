/*
 * Browser-side WebUSB transport. Opens and claims the vendor interface,
 * maintains connection liveness through heartbeats, reads length-prefixed
 * protobuf packets, and passes decoded NetworkPackets to its caller.
 */

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
const heartbeatIntervalMs = 500;
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
    const disconnectHandlerRef = useRef<((event: USBConnectionEvent) => void) | null>(null);
    const heartbeatTimerRef = useRef<number | null>(null);
    // Manual disconnection waits for this so its signal cannot race an active heartbeat.
    const heartbeatPromiseRef = useRef<Promise<void> | null>(null);
    const keepReadingRef = useRef(false);

    const [connected, setConnected] = useState(false);

    function stopHeartbeat() {
        if (heartbeatTimerRef.current === null) return;

        window.clearTimeout(heartbeatTimerRef.current);
        heartbeatTimerRef.current = null;
    }

    function finishDisconnection(device: USBDevice) {
        if (deviceRef.current !== device) return;

        stopHeartbeat();

        if (disconnectHandlerRef.current !== null) {
            navigator.usb.removeEventListener("disconnect", disconnectHandlerRef.current);
        }

        deviceRef.current = null;
        readPromiseRef.current = null;
        disconnectHandlerRef.current = null;
        heartbeatPromiseRef.current = null;
        keepReadingRef.current = false;
        setConnected(false);
    }

    async function sendConnectionSignal(device: USBDevice, connected: boolean) {
        const result = await device.controlTransferOut({
            requestType: "class",
            recipient: "interface",
            request: setControlLineStateRequest,
            value: connected ? 1 : 0,
            index: vendorInterfaceNumber,
        });

        if (result.status !== "ok") {
            throw new Error(`WebUSB connection signal failed with status: ${result.status}`);
        }
    }

    async function handleConnectionFailure(device: USBDevice, error: unknown) {
        if (deviceRef.current !== device) return;

        console.error(error);
        finishDisconnection(device);
        if (!device.opened) return;

        try {
            await device.close();
        } catch (closeError) {
            console.error(closeError);
        }
    }

    function scheduleHeartbeat(device: USBDevice) {
        heartbeatTimerRef.current = window.setTimeout(() => {
            heartbeatTimerRef.current = null;

            const heartbeatPromise = sendHeartbeat(device);
            heartbeatPromiseRef.current = heartbeatPromise;

            void heartbeatPromise.finally(() => {
                if (heartbeatPromiseRef.current === heartbeatPromise) {
                    heartbeatPromiseRef.current = null;
                }
            });
        }, heartbeatIntervalMs);
    }

    async function sendHeartbeat(device: USBDevice) {
        if (!keepReadingRef.current || deviceRef.current !== device) return;

        try {
            await sendConnectionSignal(device, true);
        } catch (error) {
            await handleConnectionFailure(device, error);
            return;
        }

        if (keepReadingRef.current && deviceRef.current === device) {
            scheduleHeartbeat(device);
        }
    }

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

            await sendConnectionSignal(device, true);

            deviceRef.current = device;
            keepReadingRef.current = true;
            setConnected(true);

            const handlePhysicalDisconnection = (event: USBConnectionEvent) => {
                if (event.device !== device) return;
                finishDisconnection(device);
            };
            disconnectHandlerRef.current = handlePhysicalDisconnection;
            navigator.usb.addEventListener("disconnect", handlePhysicalDisconnection);

            readPromiseRef.current = readFromDevice(device);
            scheduleHeartbeat(device);
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
            while (keepReadingRef.current && deviceRef.current === device) {
                const result = await device.transferIn(
                    vendorEndpointNumber,
                    vendorEndpointBufferSize,
                );

                if (result.status !== "ok") {
                    throw new Error(`WebUSB read failed with status: ${result.status}`);
                }
                if (!result.data) continue;

                const receivedBytes = new Uint8Array(
                    result.data.buffer,
                    result.data.byteOffset,
                    result.data.byteLength,
                );
                const combinedBuffer = new Uint8Array(receiveBuffer.length + receivedBytes.length);

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
        } catch (error) {
            if (!keepReadingRef.current || deviceRef.current !== device) return;
            await handleConnectionFailure(device, error);
        }
    }

    async function disconnect() {
        const device = deviceRef.current;

        if (!device) {
            console.error("disconnect(): WebUSB device unavailable");
            return;
        }

        keepReadingRef.current = false;
        stopHeartbeat();

        const heartbeatPromise = heartbeatPromiseRef.current;
        if (heartbeatPromise !== null) {
            await heartbeatPromise;
        }

        if (deviceRef.current !== device) return;

        try {
            await sendConnectionSignal(device, false);
        } catch (error) {
            console.error(error);
        }

        try {
            await device.close();
        } catch (error) {
            console.error(error);
        }

        await readPromiseRef.current;
        finishDisconnection(device);
    }

    return {
        connected,
        connect,
        disconnect,
    };
}
