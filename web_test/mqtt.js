let cy;
window.onload = function () {
  cy = window.cy = cytoscape({
    container: document.getElementById('cy'),
    boxSelectionEnabled: false,
    style: [
      {
        selector: 'node',
        css: {
          'content': 'data(id)',
          'text-valign': 'center',
          'text-halign': 'center',
          'padding': '10px',
          'shape': 'round-rectangle'
        }
      },
      {
        selector: ':parent',
        css: {
          'text-valign': 'top',
          'text-halign': 'center',
          'padding': '25px'
        }
      },
      {
        selector: 'edge',
        css: {
          'curve-style': 'bezier',
          'target-arrow-shape': 'triangle'
        }
      }
    ],
    layout: {
      name: 'preset',
      padding: 5
    },
    elements: []
  });
}

const client = new Paho.MQTT.Client("ws://127.0.0.1:9001/", "client_js_" + new Date().getTime());

const topic = "$SYS/graph";

client.onConnectionLost = onConnectionLost;
client.onMessageArrived = onMessageArrived;

client.connect({ onSuccess: onConnect });

let count = 0;
function onConnect() {
  console.log("onConnect");
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

function onMessageArrived(message) {
  var newJSON = JSON.parse(message.payloadString);

  for (var i = 0; i < newJSON.length; i++) {
    var elem = cy.getElementById(newJSON[i]["data"]["id"]);
    newJSON[i]["position"] = elem.position();
  }

  console.log(message.payloadString);

  cy.json({ elements: newJSON });

}
