from flask import Flask, render_template

app = Flask(__name__)

@app.route("/")
@app.route("/index")
def index():
    data = {"bridge": 0, "spatial": 0}
    return render_template("index.html", data=data)

@app.route("/bridge")
def bridge():
    data = {"bridge": 1, "spatial": 0}
    return render_template("index.html", data=data)

@app.route("/spatial")
def spatial():
    data = {"bridge": 0, "spatial": 1}
    return render_template("index.html", data=data)

@app.route("/spatial/bridge")
def spatial_bridge():
    data = {"bridge": 1, "spatial": 1}
    return render_template("index.html", data=data)

if __name__ == "__main__":
    app.run(debug=True, port=8000)
