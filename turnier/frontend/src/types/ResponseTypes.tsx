export type Member = {
    id: number,
    name: string,
    alias: string,
    verifiedUntil: string,
    reference: string
}

export type Participant = {
    startNumber: number,
    sorting: number,
    name: string,
    isYouth: boolean,
    club: string,
    dog: string,
    skillLevel: SkillLevel,
    size: Size,
    resultA: Result,
    resultJ: Result,
    mail?: string,
    association?: string,
    associationMemberNumber?: string,
    chipNumber?: string,
    measureDog?: boolean,
    registered?: boolean,
    ready?: boolean,
    paid?: boolean,
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
    type: 0 | 1, // 0 = A, 1 = J
}

export type ExtendedResult = {
    result: Result;
    participant: Participant;
    rank: number;
    timefaults: number;
}

export type Tournament = {
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
    speed: number,
    isGame: boolean
}

export type Organization = {
    name: string,
    turnaments: Tournament[]
}

export const ALL_RUNS = [Run.A3, Run.A2, Run.A1, Run.A0, Run.J3, Run.J2, Run.J1, Run.J0]
export const ALL_HEIGHTS = [Size.Small, Size.Medium, Size.Intermediate, Size.Large]
export const defaultParticipant: Participant = {
    startNumber: 0,
    sorting: 0,
    name: "",
    club: "",
    dog: "",
    isYouth: false,
    skillLevel: SkillLevel.A0,
    size: Size.Small,
    resultA: { time: 0, faults: 0, refusals: 0, type: 0 },
    resultJ: { time: 0, faults: 0, refusals: 0, type: 1 }
}

export type StickerInfo = {
    organization: Organization,
    turnament: Tournament,
    participant: Participant,
    finalResult: FinalResult,
    orgName: string
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

export type Verification = {
    validUntil: string,
    signedValidation: string,
    name: string,
    alias: string
}

export const runs = [Run.A3, Run.J3, Run.A2, Run.J2, Run.A1, Run.J1, Run.A0, Run.J0]
export const heights = [Size.Small, Size.Medium, Size.Intermediate, Size.Large]
export const refreshIntervall = 3000