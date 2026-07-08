import { useSerial } from "./useSerial";

function ConnectButton() {
    const { connected, connect, disconnect } = useSerial();

    return connected ? (
        <>
            <button onClick={disconnect}>Disconnect</button>
        </>
    ) : (
        <div style={{ gap: 12, display: "flex" }}>
            <button>Connect WebUSB</button>
            <button onClick={connect}>Connect WebSerial</button>
        </div>
    );
}

export default ConnectButton;
