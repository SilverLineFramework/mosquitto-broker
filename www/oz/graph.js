document.addEventListener('DOMContentLoaded', function() {
    var cy = window.cy = cytoscape({
        container: document.getElementById('cy'),
        boxSelectionEnabled: false,

        style: [{
            selector: 'node',
            style: {
                'content': 'data(id)',
                'text-valign': 'center',
                'text-halign': 'center',
                'text-wrap': 'wrap',
                'font-family' : 'Courier',
                'text-outline-width': 0.5
            }
        }, {
            selector: 'node[class="client"]',
            style: {
                'content': function(elem) {
                    let additional_info = "";
                    if (elem.data('latency') !== null && elem.data('latency') !== undefined) {
                        additional_info = "\n(" + elem.data('latency') + " ms)";
                    }
                    return elem.data('id') + additional_info;
                },
                "font-size": 3.5,
                'shape': 'round-rectangle',
                'background-color': 'Coral',
                'text-outline-color': 'Coral'
            }
        }, {
            selector: 'node[class="topic"]',
            style: {
                "font-size": 3.5,
                'shape': 'ellipse',
                'background-color': 'LightBlue',
                'text-outline-color': 'LightBlue'
            }
        }, {
            selector: 'node[class="ip"]',
            style: {
                "font-size": 4.0,
                'shape': 'barrel',
                'background-color': '#9e9199',
                'text-outline-color': '#9e9199'
            }
        }, {
            selector: ':parent',
            style: {
                'text-valign': 'bottom',
                'text-halign': 'center',
                'text-margin-y': -5
            }
        }, {
            selector: 'edge',
            style: {
                'content': function(elem) {
                    return elem.data('bps') + " bytes/s";
                },
                "font-size": 3.5,
                'width': 1.0,
                'arrow-scale': 0.5,
                'font-family' : 'Courier',
                'line-color': 'LightGray',
                'target-arrow-color': 'LightGray',
                'curve-style': 'bezier',
                // 'text-rotation': 'autorotate',
                'target-arrow-shape': 'triangle-backcurve',
                'text-outline-width': 0.5,
                'text-outline-color': 'LightGray'
            }
        }]
    });

    const brokerAddr = "wss://oz.andrew.cmu.edu/mqtt/";
    const client = new Paho.MQTT.Client(brokerAddr, "graphViewer-" + (+new Date).toString(36));
    const graphTopic = "$GRAPH";

    let prevJSON = [];
    let paused = false;

    let spinner = document.querySelector(".refreshSpinner");
    let uptodate = document.getElementById("uptodate");

    client.onConnectionLost = onConnectionLost;
    client.onMessageArrived = onMessageArrived;

    client.connect({ onSuccess: onConnect });

    function onConnect() {
        console.log("Connected!");
        client.subscribe(graphTopic);
        publish(client, "$GRAPH/latency", "", 2);
        setInterval(() => {
            publish(client, "$GRAPH/latency", "", 2);
        }, 10000);
    }

    function onConnectionLost(responseObject) {
        if (responseObject.errorCode !== 0) {
            console.log("onConnectionLost:" + responseObject.errorMessage);
        }
        spinner.style.display = "none";
        uptodate.style.display = "block";
        uptodate.innerText = "Connecton lost. Refresh to try again.";
        // client.connect({ onSuccess: onConnect });
    }

    function publish(client, dest, msg, qos) {
        let message = new Paho.MQTT.Message(msg);
        message.destinationName = dest;
        message.qos = qos;
        client.send(message);
    }

    function runLayout() {
        cy.layout({
            name: 'fcose',
            padding: 50,
            fit: true,
            animate: true,
            nodeRepulsion: 4500,
            idealEdgeLength: 50,
            tile: true,
            tilingPaddingVertical: 10,
            tilingPaddingHorizontal: 10,
            animationDuration: 100,
            animationEasing: 'ease-out'
        }).run();
    }

    function onMessageArrived(message) {
        var newJSON = JSON.parse(message.payloadString);
        try {
            if (newJSON != undefined && newJSON.length > 0) {
                if (!paused) {
                    cy.json({ elements: newJSON });
                    runLayout();
                }
                prevJSON.push(newJSON);
            }
            spinner.style.display = "none";
            uptodate.style.display = "block";
            if (!paused) {
                setTimeout(() => {
                    spinner.style.display = "block";
                    uptodate.style.display = "none";
                }, 2000);
            }
        }
        catch (err) {
            console.log(err.message);
            console.log(JSON.stringify(newJSON, undefined, 4));
        }
    }

    document.getElementById("pause").addEventListener("click", function() {
        paused = !paused;
        if (paused) {
            this.innerText = "Unpause";
            spinner.style.display = "none";
            uptodate.style.display = "block";
        }
        else {
            this.innerText = "Pause";
            spinner.style.display = "block";
            uptodate.style.display = "none";
        }
    });

    // cy.on("tap", "node", function(event) {
    //     let obj = event.target;
    //     let tapped_node = cy.$id(obj.id()).data();
    //     console.log(tapped_node["id"]);
    // });
});
