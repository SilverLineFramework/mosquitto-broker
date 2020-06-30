from flask import Flask, render_template

app = Flask(__name__)

@app.route("/debug/<ip>/<port>/")
def index(ip, port):
    data = {"ip": ip, "port": int(port)}
    return render_template("index.html", data=data)

if __name__ == "__main__":
    app.run(debug=True, port=8000)
