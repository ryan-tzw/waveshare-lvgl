import { useSerial } from "./useSerial";

type ConnectButtonProps = {
    webUsbConnected: boolean;
    connectWebUsb: () => Promise<void>;
    disconnectWebUsb: () => Promise<void>;
};

// todo: not that impt atm but change this name to smth more fitting later
export function ConnectButton({
    webUsbConnected,
    connectWebUsb,
    disconnectWebUsb,
}: ConnectButtonProps) {
    const serial = useSerial();

    return (
        <div style={{ gap: 12, display: "flex" }}>
            <button onClick={webUsbConnected ? disconnectWebUsb : connectWebUsb}>
                {webUsbConnected ? "Disconnect WebUSB" : "Connect WebUSB"}
            </button>
            <button onClick={serial.connected ? serial.disconnect : serial.connect}>
                {serial.connected ? "Disconnect WebSerial" : "Connect WebSerial"}
            </button>
        </div>
    );
}
