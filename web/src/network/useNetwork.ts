/*
 * Browser-side network state. Validates decoded NetworkPackets, stores the
 * connected gateway, LINK_STATE database, and latest device states, then
 * exposes WebUSB connection controls and a derived network graph.
 */

import { useEffect, useReducer } from "react";

import type { NetworkPacket } from "../generated/protocol_pb";
import { deriveNetworkGraph, nodeIdToHex } from "./networkGraph";
import { useUsb } from "./useUsb";

type NetworkState = {
    gateway: NetworkPacket | null;
    linkStates: Map<string, NetworkPacket>;
    deviceStates: Map<string, NetworkPacket>;
};

type NetworkAction =
    | { type: "gatewayConnected"; packet: NetworkPacket }
    | { type: "linkStateReceived"; nodeId: string; packet: NetworkPacket }
    | {
          type: "deviceStateReceived";
          nodeId: string;
          destinationGatewayNodeId: string;
          packet: NetworkPacket;
      }
    | { type: "disconnected" };

function networkReducer(state: NetworkState, action: NetworkAction): NetworkState {
    switch (action.type) {
        case "gatewayConnected": {
            return {
                gateway: action.packet,
                linkStates: new Map(),
                deviceStates: new Map(),
            };
        }
        case "linkStateReceived": {
            const previousLinkState = state.linkStates.get(action.nodeId);
            const linkStates = new Map(state.linkStates);
            linkStates.set(action.nodeId, action.packet);

            if (
                previousLinkState !== undefined &&
                previousLinkState.bootId !== action.packet.bootId
            ) {
                const deviceStates = new Map(state.deviceStates);
                deviceStates.delete(action.nodeId);

                return {
                    ...state,
                    linkStates,
                    deviceStates,
                };
            }

            return {
                ...state,
                linkStates,
            };
        }
        case "deviceStateReceived": {
            if (
                state.gateway === null ||
                nodeIdToHex(state.gateway.sourceNodeId) !== action.destinationGatewayNodeId
            ) {
                return state;
            }

            const linkState = state.linkStates.get(action.nodeId);

            if (linkState === undefined || linkState.bootId !== action.packet.bootId) {
                return state;
            }

            const previousDeviceState = state.deviceStates.get(action.nodeId);

            if (
                previousDeviceState !== undefined &&
                previousDeviceState.bootId === action.packet.bootId &&
                previousDeviceState.sequence >= action.packet.sequence
            ) {
                return state;
            }

            const deviceStates = new Map(state.deviceStates);
            deviceStates.set(action.nodeId, action.packet);

            return {
                ...state,
                deviceStates,
            };
        }
        case "disconnected": {
            return {
                gateway: null,
                linkStates: new Map(),
                deviceStates: new Map(),
            };
        }
    }
}

export function useNetwork() {
    const [state, dispatch] = useReducer(networkReducer, {
        gateway: null,
        linkStates: new Map(),
        deviceStates: new Map(),
    });

    const handlePacket = (packet: NetworkPacket) => {
        if (packet.sourceNodeId.length !== 8) {
            console.error("WebUSB packet contains an invalid source node ID");
            return;
        }

        const nodeId = nodeIdToHex(packet.sourceNodeId);
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
                        const neighborId = nodeIdToHex(neighbor.nodeId);
                        return `${neighborId} (${neighbor.localPort}->${neighbor.remotePort})`;
                    })
                    .join(", ");

                console.log(
                    `LINK_STATE received: ${nodeId} (boot ${bootId}, sequence ${packet.sequence}) -> [${adjacencyList}]`,
                );
                dispatch({ type: "linkStateReceived", nodeId, packet });
                return;
            }
            case "routedMessage": {
                const routedMessage = packet.payload.value;

                if (routedMessage.destinationGatewayNodeId.length !== 8) {
                    console.error(
                        "Routed DEVICE_STATE contains an invalid destination gateway node ID",
                    );
                    return;
                }

                if (routedMessage.deviceState === undefined) {
                    console.error("Routed DEVICE_STATE does not contain device state");
                    return;
                }

                dispatch({
                    type: "deviceStateReceived",
                    nodeId,
                    destinationGatewayNodeId: nodeIdToHex(routedMessage.destinationGatewayNodeId),
                    packet,
                });
                return;
            }
            default: {
                console.error("Unsupported WebUSB payload:", packet.payload.case);
            }
        }
    };

    const usb = useUsb(handlePacket);
    const graph = deriveNetworkGraph(state.gateway, state.linkStates, state.deviceStates);

    useEffect(() => {
        if (!usb.connected) {
            dispatch({ type: "disconnected" });
        }
    }, [usb.connected]);

    return {
        webUsbConnected: usb.connected,
        connectWebUsb: usb.connect,
        disconnectWebUsb: usb.disconnect,
        graph,
    };
}
