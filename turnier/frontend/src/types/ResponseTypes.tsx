export type Member = {
    id: number,
    name: string,
    balance: number,
    hidden: boolean,
    alias: string
}

export type Participant = {
    startNumber: number,
    sorting: number,
    name: string,
    club: string,
    dog: string,
    class: SkillLevel,
    size: Size,
    resultA: Result,
    resultJ: Result
}

export enum RunCategory {
    A,
    J
}


export enum SkillLevel {
    A0,
    A1,
    A2,
    A3,
}

export enum Run {
    A0,
    J0,
    A1,
    J1,
    A2,
    J2,
    A3,
    J3
}

export enum Size {
    Small,
    Medium,
    Intermediate,
    Large
}

export type Result = {
    time: number,
    faults: number,
    refusals: number,
    class: Run,
}

export type ExtendedResult = {
    result: Result;
    participant: Participant;
    rank: number;
    timefaults: number;
}

export type Turnament = {
    date: Date,
    judge: string,
    name: string,
    participants: Participant[],
    runs: RunInformation[]
}

export type RunInformation = {
    run: Run,
    height: Size,
    length: number,
    speed: number
}

export type Organization = {
    name: string,
    turnaments: Turnament[]
}

export const ALL_RUNS = [Run.A3, Run.A2, Run.A1, Run.A0, Run.J3, Run.J2, Run.J1, Run.J0]
export const ALL_HEIGHTS = [Size.Small, Size.Medium, Size.Intermediate, Size.Large]
export const defaultParticipant: Participant = {
    startNumber: 0,
    sorting: 0,
    name: "",
    club: "",
    dog: "",
    class: SkillLevel.A0,
    size: Size.Small,
    resultA: { time: 0, faults: 0, refusals: 0, class: Run.A0 },
    resultJ: { time: 0, faults: 0, refusals: 0, class: Run.J0 }
}

export type StickerInfo = {
    organization: Organization,
    turnament: Turnament,
    participant: Participant,
    finalResult: FinalResult
}


export type FinalResult = {
    resultA: Result & { place: number, size: Size, speed: number, timefaults: number, numberofparticipants: number },
    resultJ: Result & { place: number, size: Size, speed: number, timefaults: number, numberofparticipants: number },
    kombi: KombiResult
}

export type KombiResult = {
    participant: Participant;
    totalFaults: number;
    totalTime: number;
    kombi: number;
}