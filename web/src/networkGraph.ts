/*
 * Pure network-graph derivation. Builds the gateway-reachable graph from the
 * latest LINK_STATE and device-state packets, accepting only reciprocal port
 * observations and excluding stale or unreachable database entries.
 */

import type { DeviceState, NetworkPacket } from "./generated/protocol_pb";

type ReportedDeviceType = NonNullable<DeviceState["state"]["case"]>;
export type NetworkDeviceType = ReportedDeviceType | "unknown" | "unselected";

export type NetworkGraphNode = {
    nodeId: string;
    isGateway: boolean;
    deviceType: NetworkDeviceType;
};

export type NetworkGraphLink = {
    nodeA: string;
    portA: number;
    nodeB: string;
    portB: number;
};

export type NetworkGraph = {
    nodes: NetworkGraphNode[];
    links: NetworkGraphLink[];
};

export function nodeIdToHex(nodeId: Uint8Array) {
    let hex = "";

    for (const byte of nodeId) {
        hex += byte.toString(16).padStart(2, "0");
    }

    return hex;
}

function createLink(
    firstNode: string,
    firstPort: number,
    secondNode: string,
    secondPort: number,
): NetworkGraphLink {
    const firstEndpointComesFirst =
        firstNode < secondNode || (firstNode === secondNode && firstPort <= secondPort);

    if (firstEndpointComesFirst) {
        return {
            nodeA: firstNode,
            portA: firstPort,
            nodeB: secondNode,
            portB: secondPort,
        };
    }

    return {
        nodeA: secondNode,
        portA: secondPort,
        nodeB: firstNode,
        portB: firstPort,
    };
}

function createLinkKey(link: NetworkGraphLink) {
    return `${link.nodeA}:${link.portA}-${link.nodeB}:${link.portB}`;
}

function getDeviceType(
    nodeId: string,
    deviceStates: Map<string, NetworkPacket>,
): NetworkDeviceType {
    const packet = deviceStates.get(nodeId);

    if (packet?.payload.case !== "routedMessage") {
        return "unknown";
    }

    const deviceState = packet.payload.value.deviceState;

    if (deviceState === undefined) {
        return "unknown";
    }

    return deviceState.state.case ?? "unselected";
}

export function deriveNetworkGraph(
    gateway: NetworkPacket | null,
    linkStates: Map<string, NetworkPacket>,
    deviceStates: Map<string, NetworkPacket>,
): NetworkGraph {
    if (gateway === null) {
        return {
            nodes: [],
            links: [],
        };
    }

    const gatewayNodeId = nodeIdToHex(gateway.sourceNodeId);
    const nodes: NetworkGraphNode[] = [
        {
            nodeId: gatewayNodeId,
            isGateway: true,
            deviceType: getDeviceType(gatewayNodeId, deviceStates),
        },
    ];
    const links: NetworkGraphLink[] = [];
    const visitedNodeIds = new Set<string>([gatewayNodeId]);
    const visitedLinkKeys = new Set<string>();
    const nodeIdsToVisit = [gatewayNodeId];

    for (let nodeIndex = 0; nodeIndex < nodeIdsToVisit.length; nodeIndex++) {
        const nodeId = nodeIdsToVisit[nodeIndex];
        const packet = linkStates.get(nodeId);

        if (packet?.payload.case !== "linkState") {
            continue;
        }

        for (const neighbor of packet.payload.value.neighbors) {
            const neighborNodeId = nodeIdToHex(neighbor.nodeId);
            const neighborPacket = linkStates.get(neighborNodeId);

            if (neighborPacket?.payload.case !== "linkState") {
                continue;
            }

            const reciprocalLinkExists = neighborPacket.payload.value.neighbors.some(
                (reciprocalNeighbor) =>
                    nodeIdToHex(reciprocalNeighbor.nodeId) === nodeId &&
                    reciprocalNeighbor.localPort === neighbor.remotePort &&
                    reciprocalNeighbor.remotePort === neighbor.localPort,
            );

            if (!reciprocalLinkExists) {
                continue;
            }

            if (!visitedNodeIds.has(neighborNodeId)) {
                visitedNodeIds.add(neighborNodeId);
                nodeIdsToVisit.push(neighborNodeId);
                nodes.push({
                    nodeId: neighborNodeId,
                    isGateway: false,
                    deviceType: getDeviceType(neighborNodeId, deviceStates),
                });
            }

            const link = createLink(
                nodeId,
                neighbor.localPort,
                neighborNodeId,
                neighbor.remotePort,
            );
            const linkKey = createLinkKey(link);

            if (!visitedLinkKeys.has(linkKey)) {
                visitedLinkKeys.add(linkKey);
                links.push(link);
            }
        }
    }

    return {
        nodes,
        links,
    };
}
