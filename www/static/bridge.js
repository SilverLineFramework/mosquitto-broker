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
            selector: 'node[class="client ip"]',
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

    const ip_main = "localhost"; // "spatial.andrew.cmu.edu";
    const port = 9000;
    const client_main = new Paho.MQTT.Client(`ws://${ip_main}:${port}/`, "client_js_" + new Date().getTime());
    const graph_topic = "$SYS/graph";

    let prevJSON = [];

    let action = null;

    let modal = document.getElementById("modalView");
    let actions = document.getElementById("actions");
    let span = document.querySelector(".close");

    let spinner = document.querySelector(".refreshSpinner");
    let uptodate = document.getElementById("uptodate")

    let ipElem = document.getElementById("ipDiv");
    let clientElem = document.getElementById("clientDiv");
    let topicElem = document.getElementById("topicDiv");
    let intervalElem = document.getElementById("intervalDiv");
    let bpsElem = document.getElementById("bpsDiv");

    let msgText = document.getElementById("msg");

    client_main.onConnectionLost = onConnectionLost;
    client_main.onMessageArrived = onMessageArrived;

    client_main.connect({ onSuccess: onConnect });

    function onConnect() {
        console.log("Connected!");
        client_main.subscribe(graph_topic);
    }

    function onConnectionLost(responseObject) {
        if (responseObject.errorCode !== 0) {
            console.log("onConnectionLost:" + responseObject.errorMessage);
        }
        spinner.style.display = "none";
        uptodate.style.display = "block";
        uptodate.innerText = "Connecton lost. Refresh to try again.";
        // client_main.connect({ onSuccess: onConnect });
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
        msgText.value = JSON.stringify(newJSON, undefined, 4);

        try {
            if (newJSON != undefined) {
                cy.json({ elements: newJSON });
                runLayout();
                prevJSON.push(newJSON);
            }
            spinner.style.display = "none";
            uptodate.style.display = "block";
            setTimeout(() => {
                spinner.style.display = "block";
                uptodate.style.display = "none";
            }, 1500);
        }
        catch (err) {
            console.log(err.message)
        }
    }

    cy.on("tap", "node", function(event) {
        let obj = event.target;
        let tapped_node = cy.$id(obj.id()).data();
        console.log(tapped_node["id"]);
    });

    // cy.on('tap', function(event) {
    //     var obj = event.target;
    // });

    function undisplayModal() {
        modal.style.display = "none";
        ipElem.style.display = "block";
        clientElem.style.display = "block";
        topicElem.style.display = "block";
        intervalElem.style.display = "block";
        bpsElem.style.display = "block";
    }

    function displayModal(action) {
        modal.style.display = "block";
        document.getElementById("title").innerText = action;
        switch (action) {
            case "Connect":
                topicElem.style.display = "none";
                intervalElem.style.display = "none";
                bpsElem.style.display = "none";
                break;
            case "Subscribe":
                intervalElem.style.display = "none";
                bpsElem.style.display = "none";
                break;
            case "Unsubscribe":
                intervalElem.style.display = "none";
                bpsElem.style.display = "none";
                break;
            case "Disconnect":
                topicElem.style.display = "none";
                intervalElem.style.display = "none";
                bpsElem.style.display = "none";
                break;
            case "Publish":
            default:
                break;
        }
    }

    window.onclick = function(event) {
        if (event.target == modal) {
            undisplayModal()
        }
    }

    span.onclick = function() {
        undisplayModal()
    }

    document.getElementById("connect").addEventListener("click", function() {
        action = "Connect";
        displayModal(action);
    });

    document.getElementById("pub").addEventListener("click", function() {
        action = "Publish";
        displayModal(action);
    });

    document.getElementById("sub").addEventListener("click", function() {
        action = "Subscribe";
        displayModal(action);
    });

    document.getElementById("unsub").addEventListener("click", function() {
        action = "Unsubscribe";
        displayModal(action);
    });

    document.getElementById("disconnect").addEventListener("click", function() {
        action = "Disconnect";
        displayModal(action);
    });

    document.getElementById("action").addEventListener("click", function() {
        performAction(action);
    });

    var clients = {};

    function performAction(action) {
        let ip = document.getElementById("addresses").value;
        let client_id = document.getElementById("client").value;
        let topic_id = document.getElementById("topic").value;
        let intervalStr = document.getElementById("interval").value;
        let interval = parseInt(intervalStr);
        let bpsStr = document.getElementById("bps").value;
        let bps = parseInt(bpsStr);

        switch (action) {
            case "Connect":
                if (clients[ip] == undefined) {
                    clients[ip] = {};
                }
                if (clients[ip][client_id] != undefined) break;

                if (client_id == "") {
                    client_id = "client_js_" + new Date().getTime();
                }

                clients[ip][client_id] = new Paho.MQTT.Client(`ws://${ip}:${port}/`, client_id);
                clients[ip][client_id].connect();
                actions.innerText = `Connected client ${client_id}!`;
                break;
            case "Publish":
                if (clients[ip] == undefined || clients[ip][client_id] == undefined ||
                    topic_id == "" || isNaN(intervalStr) || isNaN(bpsStr)) break;

                if (clients[ip][client_id]["pub"]) {
                    clearInterval(clients[ip][client_id]["pub"]);
                }

                var pubmsg = "x".repeat(bps);
                publish(clients[ip][client_id], topic_id, pubmsg);
                clients[ip][client_id]["pub"] = setInterval(() => {
                    publish(clients[ip][client_id], topic_id, pubmsg);
                }, interval);
                actions.innerText = `Client ${client_id} published to ${topic_id}!`;
                break;
            case "Subscribe":
                if (clients[ip] == undefined || clients[ip][client_id] == undefined ||
                    topic_id == "") break;

                clients[ip][client_id].subscribe(topic_id);
                actions.innerText = `Client ${client_id} subscribed to ${topic_id}!`;
                break;
            case "Unsubscribe":
                if (clients[ip] == undefined || clients[ip][client_id] == undefined ||
                    topic_id == "") break;

                clients[ip][client_id].unsubscribe(topic_id);
                actions.innerText = `Client ${client_id} unsubscribed to ${topic_id}!`;
                break;
            case "Disconnect":
                if (clients[ip] == undefined ||
                    clients[ip][client_id] == undefined) break;

                if (clients[ip][client_id]["pub"]) {
                    clearInterval(clients[ip][client_id]["pub"]);
                }

                clients[ip][client_id].disconnect();
                delete clients[ip][client_id];
                actions.innerText = `Disconnected client ${client_id}!`;
                break;
            default:
                break;
        }
    }
});
