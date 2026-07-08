import { useRef, useState } from "react";

const isWebSerialSupported = () => "serial" in navigator;
const usbVendorId = 0xcafe;

export function useSerial() {
    // serial stuff won't trigger rerenders
    const portRef = useRef<SerialPort | null>(null);
    const closedPromiseRef = useRef<Promise<void> | null>(null);
    const readerRef = useRef<ReadableStreamDefaultReader<Uint8Array<ArrayBufferLike>> | null>(null);
    const keepReadingRef = useRef(false);

    // ui state for react rendering
    const [connected, setConnected] = useState(false);

    async function connect() {
        if (!isWebSerialSupported) {
            console.error("connect(): Serial port unavailable");
            return;
        }

        try {
            const port = await navigator.serial.requestPort({
                filters: [{ usbVendorId }],
            });
            await port.open({ baudRate: 115200 });
            portRef.current = port;

            setConnected(true);
            keepReadingRef.current = true;
        } catch (err) {
            console.error(err);
        }

        closedPromiseRef.current = loop();
    }

    async function loop() {
        if (!portRef.current) return;

        const decoder = new TextDecoder();

        while (portRef.current.readable && keepReadingRef.current) {
            readerRef.current = portRef.current.readable.getReader();
            try {
                while (true) {
                    const { value, done } = await readerRef.current.read();
                    if (done) break;

                    const text = decoder.decode(value);

                    // todo: actual logic instead of just printing text
                    console.log(text);
                }
            } catch (error) {
                console.error(error);
            } finally {
                readerRef.current.releaseLock();
            }
        }

        await portRef.current.close();
    }

    async function disconnect() {
        if (!portRef.current) {
            console.error("disconnect(): Serial port unavailable");
            return;
        }

        keepReadingRef.current = false;
        readerRef.current?.cancel();
        await closedPromiseRef.current;
        setConnected(false);
    }

    return {
        connected,
        connect,
        disconnect,
    };
}
