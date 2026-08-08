import { useEffect, useRef } from "react";

import type { NetworkGraph } from "./networkGraph";

type NetworkGraphViewProps = {
    graph: NetworkGraph;
};

type Point = {
    x: number;
    y: number;
};

const minimumCanvasWidth = 600;
const minimumCanvasHeight = 400;
const horizontalNodeSpacing = 190;
const verticalNodeSpacing = 100;

function createNodePositions(graph: NetworkGraph, canvasWidth: number, canvasHeight: number) {
    const positions = new Map<string, Point>();
    const columnCount = Math.max(1, Math.ceil(Math.sqrt(graph.nodes.length)));
    const rowCount = Math.max(1, Math.ceil(graph.nodes.length / columnCount));
    const firstRowY = canvasHeight / 2 - ((rowCount - 1) * verticalNodeSpacing) / 2;

    for (let nodeIndex = 0; nodeIndex < graph.nodes.length; nodeIndex++) {
        const row = Math.floor(nodeIndex / columnCount);
        const column = nodeIndex % columnCount;
        const nodesInRow = Math.min(columnCount, graph.nodes.length - row * columnCount);
        const firstColumnX = canvasWidth / 2 - ((nodesInRow - 1) * horizontalNodeSpacing) / 2;

        positions.set(graph.nodes[nodeIndex].nodeId, {
            x: firstColumnX + column * horizontalNodeSpacing,
            y: firstRowY + row * verticalNodeSpacing,
        });
    }

    return positions;
}

export function NetworkGraphView({ graph }: NetworkGraphViewProps) {
    const canvasRef = useRef<HTMLCanvasElement | null>(null);
    const columnCount = Math.max(1, Math.ceil(Math.sqrt(graph.nodes.length)));
    const rowCount = Math.max(1, Math.ceil(graph.nodes.length / columnCount));
    const canvasWidth = Math.max(minimumCanvasWidth, columnCount * horizontalNodeSpacing);
    const canvasHeight = Math.max(minimumCanvasHeight, rowCount * verticalNodeSpacing);

    useEffect(() => {
        const canvas = canvasRef.current;

        if (!canvas) {
            return;
        }

        const context = canvas.getContext("2d");

        if (!context) {
            return;
        }

        const rootStyles = getComputedStyle(document.documentElement);
        const backgroundColor = rootStyles.getPropertyValue("--bg").trim();
        const borderColor = rootStyles.getPropertyValue("--border").trim();
        const textColor = rootStyles.getPropertyValue("--text-h").trim();
        const accentColor = rootStyles.getPropertyValue("--accent").trim();
        const accentBackgroundColor = rootStyles.getPropertyValue("--accent-bg").trim();
        const monospaceFont = rootStyles.getPropertyValue("--mono").trim();

        context.clearRect(0, 0, canvasWidth, canvasHeight);
        context.font = `14px ${monospaceFont}`;
        context.textAlign = "center";
        context.textBaseline = "middle";

        if (graph.nodes.length === 0) {
            context.fillStyle = textColor;
            context.fillText(
                "Connect a gateway to display the network",
                canvasWidth / 2,
                canvasHeight / 2,
            );
            return;
        }

        const positions = createNodePositions(graph, canvasWidth, canvasHeight);

        context.strokeStyle = borderColor;
        context.lineWidth = 2;

        for (const link of graph.links) {
            const firstPosition = positions.get(link.nodeA);
            const secondPosition = positions.get(link.nodeB);

            if (!firstPosition || !secondPosition) {
                continue;
            }

            context.beginPath();
            context.moveTo(firstPosition.x, firstPosition.y);
            context.lineTo(secondPosition.x, secondPosition.y);
            context.stroke();
        }

        const horizontalPadding = 12;
        const nodeHeight = 40;

        for (const node of graph.nodes) {
            const position = positions.get(node.nodeId);

            if (!position) {
                continue;
            }

            const nodeWidth = context.measureText(node.nodeId).width + horizontalPadding * 2;
            const left = position.x - nodeWidth / 2;
            const top = position.y - nodeHeight / 2;

            context.fillStyle = backgroundColor;
            context.fillRect(left, top, nodeWidth, nodeHeight);

            if (node.isGateway) {
                context.fillStyle = accentBackgroundColor;
                context.fillRect(left, top, nodeWidth, nodeHeight);
            }

            context.strokeStyle = node.isGateway ? accentColor : borderColor;
            context.lineWidth = node.isGateway ? 3 : 2;
            context.strokeRect(left, top, nodeWidth, nodeHeight);

            context.fillStyle = textColor;
            context.fillText(node.nodeId, position.x, position.y);
        }
    }, [canvasHeight, canvasWidth, graph]);

    return (
        <section className="network-graph-view">
            <canvas
                ref={canvasRef}
                className="network-graph-canvas"
                width={canvasWidth}
                height={canvasHeight}
            />
        </section>
    );
}
