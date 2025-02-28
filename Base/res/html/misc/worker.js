onmessage = evt => {
    console.log("In Worker - Got message:", JSON.stringify(evt.data));

    postMessage(evt.data, null);
};

console.log("In Worker - Loaded", this);
console.log("Keys: ", JSON.stringify(Object.keys(this)));

postMessage("loaded", null);
