document.addEventListener('DOMContentLoaded', function() {
    var cy = window.cy = cytoscape({
        container: document.getElementById('cy'),
        boxSelectionEnabled: false,

        style: [{
            selector: 'node',
            style: {
                'content': 'data(label)',
                'text-valign': 'center',
                'text-halign': 'center',
                'font-family' : 'Courier',
                'text-outline-width': 0.5
            }
        }, {
            selector: 'node[class="client"]',
            style: {
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
                'label': 'data(label)',
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

    const clientMain = new Paho.MQTT.Client("wss://oz.andrew.cmu.edu/mqtt/", "graph_" + new Date().getTime());
    const graphTopic = "$SYS/graph";

    let prevJSON = [];
    let paused = false;

    let spinner = document.querySelector(".refreshSpinner");
    let uptodate = document.getElementById("uptodate");

    clientMain.onConnectionLost = onConnectionLost;
    clientMain.onMessageArrived = onMessageArrived;

    clientMain.connect({ onSuccess: onConnect });

    function onConnect() {
        console.log("Connected!");
        clientMain.subscribe(graphTopic);
    }

    function onConnectionLost(responseObject) {
        if (responseObject.errorCode !== 0) {
            console.log("onConnectionLost:" + responseObject.errorMessage);
        }
        spinner.style.display = "none";
        uptodate.style.display = "block";
        uptodate.innerText = "Connecton lost. Refresh to try again.";
        // clientMain.connect({ onSuccess: onConnect });
    }

    function publish(client, dest, msg) {
        let message = new Paho.MQTT.Message(msg);
        message.destinationName = dest;
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
            if (newJSON != undefined) {
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
                }, 1500);
            }
        }
        catch (err) {
            console.log(err.message)
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
