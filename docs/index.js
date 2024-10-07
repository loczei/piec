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
                max: 25.0
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
        scales: {
            y: {
                min: 40.0,
                max: 130.0
            }
        }
    }
});

let temp_ip = localStorage.getItem("ip");

if(temp_ip) {
    document.querySelector("#ip").value = temp_ip;
}

document.querySelector("#scale").addEventListener("change", () => {
    if (document.querySelector("#scale").checked) {
        oxygen.options.scales.y.min = 0.0;
        oxygen.options.scales.y.max = 25.0;

        servo.options.scales.y.min = 40.0;
        servo.options.scales.y.max = 180.0;
    } else {
        oxygen.options.scales.y.min = null;
        oxygen.options.scales.y.max = null;

        servo.options.scales.y.min = null;
        servo.options.scales.y.max = null;
    }
});

let history = 200;

document.querySelector("#history-button").addEventListener("click", () => {
    history = Number(document.querySelector("#history").value);
});

let socket;

const ipButton = document.querySelector('#ip-button');

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

        if (oxygen.data.datasets[0].data.length > history) {
            oxygen.data.datasets[0].data = oxygen.data.datasets[0].data.slice(oxygen.data.datasets[0].data.length - history);
            oxygen.data.datasets[1].data = oxygen.data.datasets[1].data.slice(oxygen.data.datasets[1].data.length - history);
            oxygen.data.labels = oxygen.data.labels.slice(oxygen.data.labels.length - history);
        }

        oxygen.update();

        servo.data.labels.push(msg.time.substring(msg.time.indexOf("T")));
        servo.data.datasets[0].data.push(msg.balance + 85);
        servo.data.datasets[1].data.push(msg.servo);

        if (servo.data.datasets[1].data.length > history) {
            servo.data.datasets[0].data = servo.data.datasets[0].data.slice(servo.data.datasets[0].data.length - history);
            servo.data.datasets[1].data = servo.data.datasets[1].data.slice(servo.data.datasets[1].data.length - history);
            servo.data.labels = servo.data.labels.slice(servo.data.labels.length - history);
        }

        servo.update();
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

document.querySelector("#zadana-button").addEventListener("click", () => {
    if (socket) {
        let str = `T ${document.querySelector("#zadana").value}`;
        socket.send(str);
    }
});

document.querySelector("#odciecie-button").addEventListener("click", () => {
    if (socket) {
        let str = `C ${document.querySelector("#zadana").value}`;
        socket.send(str);
    }
});