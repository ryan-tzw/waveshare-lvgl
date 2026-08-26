import "./App.css";
import { ConnectButton } from "./ConnectButton";
import { useNetwork } from "./network/useNetwork";
import { NetworkGraphView } from "./rendering/NetworkGraphView";

function App() {
    const network = useNetwork();

    return (
        <>
            <section id="center">
                <h1>🦆🦆🦆</h1>
                <ConnectButton
                    webUsbConnected={network.webUsbConnected}
                    connectWebUsb={network.connectWebUsb}
                    disconnectWebUsb={network.disconnectWebUsb}
                />
            </section>

            <NetworkGraphView graph={network.graph} />
        </>
    );
}

export default App;
