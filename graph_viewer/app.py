from flask import Flask, render_template

app = Flask(__name__)

@app.route("/")
def index():
    data = {"oz": 1, "ip": 0, "port": 0}
    return render_template("index.html", data=data)

@app.route("/debug/<ip>/<port>/")
def debug(ip, port):
    data = {"oz": 0, "ip": ip, "port": int(port)}
    return render_template("index.html", data=data)

if __name__ == "__main__":
    app.run(debug=True, port=8000)
