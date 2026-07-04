import { useState } from "react";

function ConnectButton() {
    const [connected, setConnected] = useState(false);

    return connected ? (
        <>
            <button>Disconnect</button>
        </>
    ) : (
        <div style={{ gap: 12, display: "flex" }}>
            <button>Connect WebUSB</button>
            <button>Connect WebSerial</button>
        </div>
    );
}

export default ConnectButton;
