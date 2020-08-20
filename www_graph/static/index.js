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

    let brokerAddr = `ws://${ip_main}:${port}/`;
    if (oz) {
        brokerAddr = "wss://oz.andrew.cmu.edu/mqtt/";
    }
    const clientMain = new Paho.MQTT.Client(brokerAddr, "graphViewer-" + (+new Date).toString(36));
    const graphTopic = "$GRAPH";

    let prevJSON = [];
    let currIdx = 0;

    let action = null;

    let modal = document.getElementById("modalView");
    let actions = document.getElementById("actions");
    let span = document.querySelector(".close");

    let spinner = document.querySelector(".refreshSpinner");
    let uptodate = document.getElementById("uptodate");

    let pauseBtn = document.getElementById("pause");
    let spinnerUpdate = true;
    let paused = false;

    let ipElem = document.getElementById("ipDiv");
    let clientElem = document.getElementById("clientDiv");
    let topicElem = document.getElementById("topicDiv");
    let intervalElem = document.getElementById("intervalDiv");
    let bpsElem = document.getElementById("bpsDiv");

    let msgText = document.getElementById("msg");

    clientMain.onConnectionLost = onConnectionLost;
    clientMain.onMessageArrived = onMessageArrived;

    clientMain.connect({ onSuccess: onConnect });

    function onConnect() {
        console.log("Connected!");
        clientMain.subscribe(graphTopic);
        publish(clientMain, "$GRAPH/latency", "", 2);
        setInterval(() => {
            publish(clientMain, "$GRAPH/latency", "", 2);
        }, 10000);
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

    function createCyJSON(json) {
        let res = [];
        let cnt = 0;
        for (let i = 0; i < json["ips"].length; i++) {
            let ip = json["ips"][i]
            let ipJSON = {};
            ipJSON["data"] = {};
            ipJSON["data"]["id"] = ip["address"];
            ipJSON["data"]["class"] = "ip";
            ipJSON["group"] = "nodes";
            res.push(ipJSON);

            for (let j = 0; j < ip["clients"].length; j++) {
                let client = ip["clients"][j];
                let clientJSON = {};
                clientJSON["data"] = {};
                clientJSON["data"]["id"] = client["name"];
                clientJSON["data"]["latency"] = client["latency"];
                clientJSON["data"]["class"] = "client";
                clientJSON["data"]["parent"] = ip["address"];
                clientJSON["group"] = "nodes";
                res.push(clientJSON);

                for (let k = 0; k < client["published"].length; k++) {
                    let pubEdge = client["published"][k];
                    let pubEdgeJSON = {};
                    pubEdgeJSON["data"] = {};
                    pubEdgeJSON["data"]["id"] = "edge_"+(cnt++);
                    pubEdgeJSON["data"]["bps"] = pubEdge["bps"];
                    pubEdgeJSON["data"]["source"] = client["name"];
                    pubEdgeJSON["data"]["target"] = pubEdge["topic"];
                    pubEdgeJSON["group"] = "edges";
                    res.push(pubEdgeJSON);
                }
            }
        }

        for (i = 0; i < json["topics"].length; i++) {
            let topic = json["topics"][i];
            let topicJSON = {};
            topicJSON["data"] = {};
            topicJSON["data"]["id"] = topic["name"];
            topicJSON["data"]["class"] = "topic";
            topicJSON["group"] = "nodes";
            res.push(topicJSON);

            for (j = 0; j < topic["subscriptions"].length; j++) {
                let subEdge = topic["subscriptions"][j];
                let subEdgeJSON = {};
                subEdgeJSON["data"] = {};
                subEdgeJSON["data"]["id"] = "edge_"+(cnt++);
                subEdgeJSON["data"]["bps"] = subEdge["bps"];
                subEdgeJSON["data"]["source"] = topic["name"];
                subEdgeJSON["data"]["target"] = subEdge["client"];
                subEdgeJSON["group"] = "edges";
                res.push(subEdgeJSON);
            }
        }
        return res;
    }

    function updateCy(json) {
        try {
            let cyJSON = createCyJSON(json);
            cy.json({ elements: cyJSON });
            runLayout();
        }
        catch (err) {
            console.log(err.message);
            console.log(JSON.stringify(json, undefined, 2));
        }
    }

    function onMessageArrived(message) {
        var newJSON = JSON.parse(message.payloadString);
        msgText.value = JSON.stringify(newJSON, undefined, 4);

        if (!paused) updateCy(newJSON);
        prevJSON.push(newJSON);
        if (!paused) currIdx = prevJSON.length;

        spinner.style.display = "none";
        uptodate.style.display = "block";
        if (!paused) {
            spinnerUpdate = false;
            setTimeout(() => {
                spinner.style.display = "block";
                uptodate.style.display = "none";
                spinnerUpdate = true;
            }, 2000);
        }
    }

    function timer() {
        if (spinnerUpdate) {
            if (paused) {
                pauseBtn.innerHTML = "&#9658;";
                spinner.style.display = "none";
                uptodate.style.display = "block";
            }
            else {
                pauseBtn.innerHTML = "&#10074;&#10074;";
                spinner.style.display = "block";
                uptodate.style.display = "none";
            }
        }
        requestAnimationFrame(timer);
    }
    timer();

    pauseBtn.addEventListener("click", function() {
        paused = !paused;
        if (currIdx != prevJSON.length-1) {
            currIdx = prevJSON.length-1;
            updateCy(prevJSON[currIdx]);
        }
    });

    document.getElementById("forward").addEventListener("click", function() {
        paused = true;
        let prevIdx = currIdx;
        currIdx++;
        if (currIdx >= prevJSON.length) {
            currIdx = prevJSON.length-1;
            paused = false;
        }
        if (prevIdx != currIdx) {
            updateCy(prevJSON[currIdx]);
        }
    });

    document.getElementById("reverse").addEventListener("click", function() {
        paused = true;
        let prevIdx = currIdx;
        currIdx--;
        if (currIdx < 0) {
            currIdx = 0;
        }
        if (prevIdx != currIdx) {
            updateCy(prevJSON[currIdx]);
        }
    });

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
                publish(clients[ip][client_id], topic_id, pubmsg, 1);
                clients[ip][client_id]["pub"] = setInterval(() => {
                    publish(clients[ip][client_id], topic_id, pubmsg, 1);
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

    cy.on("tap", "node", function(event) {
        let obj = event.target;
        let tapped_node = cy.$id(obj.id()).data();
        console.log(tapped_node["id"]);
    });

    // cy.on('tap', function(event) {
    //     var obj = event.target;
    // });
});
