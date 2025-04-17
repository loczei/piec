const oxygen_ctx = document.getElementById('tlen');

let oxygen = new Chart(oxygen_ctx, {
    type: 'line',
    data: {
        labels: [],
        datasets: [{
            label: 'Poziom tlenu',
            data: [],
            fill: false,
        },
        {
            label: 'Zadany tlen',
            data: [],
            fill: false,
        }]
    },
    options: {
        animation: {
            duration: 0,
        },
        scales: {
            y: {
                min: 0.0,
                max: 23.0
            }
        }
    }
});

const servo_ctx = document.getElementById('serwo');

let servo = new Chart(servo_ctx, {
    type: 'line',
    data: {
        labels: [],
        datasets: [{
            label: 'Punkt równowagi',
            data: [],
            fill: false,
        },
        {
            label: 'Wychylenie serwa',
            data: [],
            fill: false,
        }]
    },
    options: {
        animation: {
            duration: 0,
        },
    }
});

const topServo_ctx = document.getElementById('serwo-gorne');

let topServo = new Chart(topServo_ctx, {
    type: 'line',
    data: {
        labels: [],
        datasets: [{
            label: 'Wychylenie serwa',
            data: [],
            fill: false,
        }]
    },
    options: {
        animation: {
            duration: 0,
        },
    }
});

let temp_ip = localStorage.getItem("ip");

if(temp_ip) {
    document.querySelector("#ip").value = temp_ip;
}

document.querySelector("#scale").addEventListener("change", () => {
    if (document.querySelector("#scale").checked) {
        oxygen.options.scales.y.min = 0.0;
        oxygen.options.scales.y.max = 23.0;
    } else {
        oxygen.options.scales.y.min = null;
        oxygen.options.scales.y.max = null;
    }
});

let history = 200;

document.querySelector("#history-button").addEventListener("click", () => {
    history = Number(document.querySelector("#history").value);
});

let socket;

const ipButton = document.querySelector('#ip-button');

function deleteHistory(target) {
    if (target.data.datasets[0].data.length > history) {
        target.data.datasets[0].data = target.data.datasets[0].data.slice(target.data.datasets[0].data.length - history);
        target.data.datasets[1].data = target.data.datasets[1].data.slice(target.data.datasets[1].data.length - history);
        target.data.labels = target.data.labels.slice(target.data.labels.length - history);
    }
}

ipButton.addEventListener("click", () => {
    console.log("Connecting");
    let ip = document.querySelector("#ip").value;
    socket = new WebSocket(`ws://${ip}`);
    ipButton.textContent = "Łączenie";

    socket.addEventListener("open", () => {
        ipButton.textContent = "Połączono";
        ipButton.active = false;
        ipButton.style.backgroundColor = "green";

        localStorage.setItem("ip", ip);
    });

    socket.addEventListener("message", (e) => {
        console.log(e.data);
        let msg = JSON.parse(e.data);
        
        oxygen.data.labels.push(msg.time.substring(msg.time.indexOf("T")));
        oxygen.data.datasets[0].data.push(msg.oxygen);
        oxygen.data.datasets[1].data.push(msg.target);

        deleteHistory(oxygen);

        oxygen.update();

        servo.data.labels.push(msg.time.substring(msg.time.indexOf("T")));
        servo.data.datasets[0].data.push(msg.balance + 85);
        servo.data.datasets[1].data.push(msg.servo);

        deleteHistory(servo);

        servo.update();

        topServo.data.labels.push(msg.time.substring(msg.time.indexOf("T")));
        topServo.data.datasets[0].data.push(msg.topServo);

        deleteHistory(topServo);

        topServo.update();
    });

    socket.addEventListener("close", () => {
        ipButton.textContent = "Połącz";
        ipButton.active = true;
        ipButton.style.backgroundColor = "#006381";
    });
    socket.addEventListener("error", () => {
        ipButton.textContent = "Połącz";
        ipButton.active = true;
        ipButton.style.backgroundColor = "#006381";
    });
});

function button(name, char, payload) {
    document.querySelector(`#${name}-button`).addEventListener("click", () => {
        if (socket) {
            let str;
            if (payload) {
                str = `${char} ${document.querySelector(`#${name}`).value}`;
            } else {
                str = `${char}`;
            }
            
            socket.send(str);
        }
    });
}

button("zadana", "T", true);
button("odciecie", "C", true);
button("odciecie-servo", "O", true);
button("pompy", "R", false);
button("serwo", "S", false);
button("min-angle", "I", true);
button("max-angle", "A", true);
button("multi-real", "U", true);
button("boost-real", "E", true);
button("multi-max", "M", true);
button("servo-balance-cooldown", "V", true);
button("balance-multi", "L", true);
button("max-balance", "X", true);