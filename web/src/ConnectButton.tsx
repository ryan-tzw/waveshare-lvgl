import { useState } from "react";
import { useSerial } from "./useSerial";

// todo: not that impt atm but change this name to smth more fitting later
export function ConnectButton() {
    const { connected, connect, disconnect, send } = useSerial();
    const [input, setInput] = useState("");

    const handleSubmit = (event: React.SubmitEvent) => {
        event.preventDefault();

        const trimmed = input.trim();
        if (!trimmed) return;

        send(trimmed);
        setInput("");
    };

    return connected ? (
        <>
            <button onClick={disconnect}>Disconnect</button>
            <form onSubmit={handleSubmit}>
                <input
                    autoComplete="off"
                    value={input}
                    onChange={(e) => setInput(e.currentTarget.value)}
                />
            </form>
        </>
    ) : (
        <>
            <div style={{ gap: 12, display: "flex" }}>
                <button disabled>Connect WebUSB</button>
                <button onClick={connect}>Connect WebSerial</button>
            </div>
        </>
    );
}
