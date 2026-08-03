import { useSerial } from "./useSerial";
import { useUsb } from "./useUsb";

// todo: not that impt atm but change this name to smth more fitting later
export function ConnectButton() {
    const serial = useSerial();
    const usb = useUsb();

    return (
        <div style={{ gap: 12, display: "flex" }}>
            <button onClick={usb.connected ? usb.disconnect : usb.connect}>
                {usb.connected ? "Disconnect WebUSB" : "Connect WebUSB"}
            </button>
            <button onClick={serial.connected ? serial.disconnect : serial.connect}>
                {serial.connected ? "Disconnect WebSerial" : "Connect WebSerial"}
            </button>
        </div>
    );
}
