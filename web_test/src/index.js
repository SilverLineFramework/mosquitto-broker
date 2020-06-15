let cytoscape = require('cytoscape');
let coseBilkent = require('cytoscape-cose-bilkent');

var ready = false;

window.onload = function () {
    cytoscape.use(coseBilkent);
    var cy = window.cy = cytoscape({
        container: document.getElementById('cy'),
        boxSelectionEnabled: false,

        style: [{
            selector: 'node',
            css: {
                'content': 'data(label)',
                'text-valign': 'center',
                'text-halign': 'center'
            }
        }, {
            selector: 'node[class="client"]',
            style: {
                "font-size": 6,
                'shape': 'round-rectangle',
                'background-color': 'LightGray'
            }
        }, {
            selector: 'node[class="topic"]',
            style: {
                "font-size": 6,
                'shape': 'ellipse',
                'background-color': 'LightBlue'
            }
        }, {
            selector: 'node[class="client ip"]',
            style: {
                "font-size": 6,
                'shape': 'barrel',
                'background-color': 'Coral'
            }
        }, {
            selector: ':parent',
            css: {
                'text-valign': 'top',
                'text-halign': 'center',
            }
        }, {
            selector: 'edge',
            css: { },
            style: {
                'label': 'data(label)',
                "font-size": 3,
                'width': 1,
                'arrow-scale': 0.5,
                'curve-style': 'bezier',
                'text-rotation': 'autorotate',
                'target-arrow-shape': 'triangle-cross'
            }
        }],
        elements: []
    });
    ready = true;
}

const client = new Paho.MQTT.Client("ws://127.0.0.1:9001/", "client_js_" + new Date().getTime());

const topic = "$SYS/graph";

client.onConnectionLost = onConnectionLost;
client.onMessageArrived = onMessageArrived;

client.connect({ onSuccess: onConnect });

function onConnect() {
    console.log("Connected!");
    client.subscribe(topic);
    // setInterval(() => { publish(topic, `The count is now ${count++}`) }, 1000)

}

function onConnectionLost(responseObject) {
    if (responseObject.errorCode !== 0) {
        console.log("onConnectionLost:" + responseObject.errorMessage);
    }
    client.connect({ onSuccess: onConnect });
}

// const publish = (dest, msg) => {
//   console.log('desint :', dest, 'msggg', msg)
//   let message = new Paho.MQTT.Message(msg);
//   message.destinationName = dest;
//   client.send(message);
// }

function runLayout() {
    cy.layout({
        name: 'cose-bilkent',
        padding: 100,
        nodeRepulsion: 5000,
        animationDuration: 100
    }).run();
}

function onMessageArrived(message) {
    if (ready) {
        console.log(message.payloadString);

        var newJSON = JSON.parse(message.payloadString);

        // for (var i = 0; i < newJSON.length; i++) {
        //     var elem = cy.getElementById(newJSON[i]["data"]["id"]);
        //     newJSON[i]["position"] = elem.position();
        // }

        cy.json({ elements: newJSON });

        runLayout();
    }
}
