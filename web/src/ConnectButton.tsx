import { useSerial } from "./useSerial";

// todo: not that impt atm but change this name to smth more fitting later
export function ConnectButton() {
    const { connected, connect, disconnect } = useSerial();

    return (
        <div style={{ gap: 12, display: "flex" }}>
            <button disabled>Connect WebUSB</button>
            <button onClick={connected ? disconnect : connect}>
                {connected ? "Disconnect WebSerial" : "Connect WebSerial"}
            </button>
        </div>
    );
}
