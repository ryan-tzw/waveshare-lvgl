import { useCallback, useReducer } from "react";

import type { NetworkPacket } from "./generated/protocol_pb";
import { useUsb } from "./useUsb";

type NetworkState = {
    gateway: NetworkPacket | null;
    linkStates: Map<string, NetworkPacket>;
};

type NetworkAction =
    | { type: "gatewayConnected"; packet: NetworkPacket }
    | { type: "linkStateReceived"; nodeId: string; packet: NetworkPacket }
    | { type: "disconnected" };

function networkReducer(state: NetworkState, action: NetworkAction): NetworkState {
    switch (action.type) {
        case "gatewayConnected": {
            return {
                gateway: action.packet,
                linkStates: new Map(),
            };
        }
        case "linkStateReceived": {
            const linkStates = new Map(state.linkStates);
            linkStates.set(action.nodeId, action.packet);

            return {
                ...state,
                linkStates,
            };
        }
        case "disconnected": {
            return {
                gateway: null,
                linkStates: new Map(),
            };
        }
    }
}

function bytesToHex(bytes: Uint8Array) {
    let hex = "";

    for (const byte of bytes) {
        hex += byte.toString(16).padStart(2, "0");
    }

    return hex;
}

export function useNetwork() {
    const [state, dispatch] = useReducer(networkReducer, {
        gateway: null,
        linkStates: new Map(),
    });

    const handlePacket = useCallback((packet: NetworkPacket) => {
        if (packet.sourceNodeId.length !== 8) {
            console.error("WebUSB packet contains an invalid source node ID");
            return;
        }

        const nodeId = bytesToHex(packet.sourceNodeId);
        const bootId = packet.bootId.toString(16).padStart(8, "0");

        switch (packet.payload.case) {
            case "gatewayHello": {
                console.log(`Gateway connected: ${nodeId} (boot ${bootId})`);
                dispatch({ type: "gatewayConnected", packet });
                return;
            }
            case "linkState": {
                const neighbors = packet.payload.value.neighbors;

                for (const neighbor of neighbors) {
                    if (neighbor.nodeId.length !== 8) {
                        console.error("LINK_STATE contains an invalid neighbor node ID");
                        return;
                    }
                }

                const adjacencyList = neighbors
                    .map((neighbor) => {
                        const neighborId = bytesToHex(neighbor.nodeId);
                        return `${neighborId} (${neighbor.localPort}->${neighbor.remotePort})`;
                    })
                    .join(", ");

                console.log(
                    `LINK_STATE received: ${nodeId} (boot ${bootId}, sequence ${packet.sequence}) -> [${adjacencyList}]`,
                );
                dispatch({ type: "linkStateReceived", nodeId, packet });
                return;
            }
            default: {
                console.error("Unsupported WebUSB payload:", packet.payload.case);
            }
        }
    }, []);

    const usb = useUsb(handlePacket);

    async function disconnectWebUsb() {
        try {
            await usb.disconnect();
        } finally {
            dispatch({ type: "disconnected" });
        }
    }

    return {
        webUsbConnected: usb.connected,
        connectWebUsb: usb.connect,
        disconnectWebUsb,
        gateway: state.gateway,
        linkStates: state.linkStates,
    };
}
