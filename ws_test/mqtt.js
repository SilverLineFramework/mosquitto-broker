var cy;
window.onload = function () {
  cy = window.cy = cytoscape({
    container: document.getElementById('cy'),

    boxSelectionEnabled: false,
    autounselectify: true,

    layout: {
      name: 'grid'
    },

    style: [
      {
        selector: 'node',
        style: {
          'height': 20,
          'width': 20,
          'background-color': '#18e018'
        }
      },

      {
        selector: 'edge',
        style: {
          'curve-style': 'haystack',
          'haystack-radius': 0,
          'width': 5,
          'opacity': 0.5,
          'line-color': '#a2efa2'
        }
      }
    ],
    elements: []
  });
}

const client = new Paho.MQTT.Client("ws://127.0.0.1:9001/", "client_js_" + new Date().getTime());

const topic = "$SYS/hello";

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
  // let el = document.createElement('div')
  // el.innerHTML = message.payloadString
  // document.body.appendChild(el)
  // console.log(message.payloadString)
  var newJSON = JSON.parse(message.payloadString)
  console.log(newJSON);
  cy.json({ elements: newJSON })
}
