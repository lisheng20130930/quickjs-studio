class Player {
    constructor(name) {
        this.name = name;
    }

    move(distance) {
        Engine.log(`${this.name} moved ${distance}`);
    }
}
