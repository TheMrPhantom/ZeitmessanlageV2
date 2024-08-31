import { AlertColor } from "@mui/material"
import { Organization, Participant, Result, Run, Size, StickerInfo, Tournament } from "../types/ResponseTypes"
import { dateToURLString } from "../Components/Common/StaticFunctions"

const defaultAlertType: AlertColor = "success"

const initialState: CommonReducerType = {

    organization: {
        name: "default",
        turnaments: []
    },
    isLoggedIn: false,
    toast: {
        open: false,
        duration: 4000,
        headline: undefined,
        message: "",
        type: defaultAlertType
    },
    resultsToPrint: [],
    participantspToPrint: [],
    stickersToPrint: [],
    userTurnament: {
        date: new Date(),
        judge: "",
        name: "",
        participants: [],
        runs: []
    }
}

export type CommonReducerType = {
    organization: Organization,
    isLoggedIn: boolean,
    toast: {
        open: boolean,
        duration: number,
        headline: string | undefined,
        message: string,
        type: AlertColor
    }
    resultsToPrint: ResultToPrint,
    participantspToPrint: ParticipantToPrint,
    stickersToPrint: StickerInfo[],
    userTurnament: Tournament
}

export type ResultToPrint = Array<{
    run: Run,
    size: Size,
    length: number,
    standardTime: number
    results: Array<{ participant: Participant, result: Result, timeFaults: number }>
}>

export type ParticipantToPrint = Array<{
    run: Run,
    size: Size,
    participants: Participant[]
}>

const reducer = (state = initialState, { type, payload }: any) => {

    var newState = { ...state }
    switch (type) {
        case "OPEN_TOAST":
            newState.toast.open = true;
            newState.toast.message = payload.message;
            newState.toast.headline = payload.headline
            newState.toast.duration = payload.duration ? payload.duration : initialState.toast.duration
            newState.toast.type = payload.type ? payload.type : defaultAlertType
            return newState

        case "CLOSE_TOAST":
            newState.toast.open = false;
            return newState

        case "CREATE_ORGANIZATION":
            newState.organization = payload
            return newState
        case "ADD_TURNAMENT":
            newState.organization.turnaments = [...newState.organization.turnaments, payload]
            return newState
        case "REMOVE_TURNAMENT":
            const newTurnaments = newState.organization?.turnaments.filter((t) => t.date !== payload.date)
            newState.organization.turnaments = newTurnaments ? newTurnaments : []
            return newState
        case "LOAD_ORGANIZATION":
            newState.organization = payload
            return newState
        case "CHANGE_LENGTH":
            newState.organization.turnaments.forEach((t) => {
                if (dateToURLString(new Date(t.date)) === dateToURLString(payload.date)) {
                    t.runs.forEach((r) => {
                        if (r.run === payload.run) {
                            r.length = payload.length
                        }
                    })
                }
            })
            return newState
        case "CHANGE_SPEED":
            newState.organization.turnaments.forEach((t) => {
                if (dateToURLString(new Date(t.date)) === dateToURLString(payload.date)) {
                    t.runs.forEach((r) => {
                        if (r.run === payload.run) {
                            r.speed = payload.speed
                        }
                    })
                }
            })
            return newState
        case "CHANGE_DATE":
            //Check if new date is already in use
            if (newState.organization.turnaments.find(t => dateToURLString(new Date(t.date)) === dateToURLString(new Date(payload.newDate)))) {
                newState.toast.open = true
                newState.toast.message = "Date already in use"
                newState.toast.headline = "Error"
                newState.toast.type = "error"
                return newState
            }
            newState.organization.turnaments.forEach((t) => {
                if (dateToURLString(new Date(t.date)) === dateToURLString(payload.oldDate)) {
                    console.log("Changing date")
                    t.date = payload.newDate
                }
            })
            return newState
        case "CHANGE_JUDGE":
            newState.organization.turnaments.forEach((t) => {
                if (dateToURLString(new Date(t.date)) === dateToURLString(payload.date)) {
                    t.judge = payload.judge
                }
            })
            return newState
        case "CHANGE_TURNAMENT_NAME":
            newState.organization.turnaments.forEach((t) => {
                if (dateToURLString(new Date(t.date)) === dateToURLString(payload.date)) {
                    t.name = payload.name
                }
            })
            return newState
        case "ADD_PARTICIPANT":
            newState.organization.turnaments.forEach((t) => {
                if (dateToURLString(new Date(t.date)) === dateToURLString(payload.date)) {
                    t.participants.push(payload.participant)
                }
            })
            return newState
        case "REMOVE_PARTICIPANT":
            newState.organization.turnaments.forEach((t) => {
                if (dateToURLString(new Date(t.date)) === dateToURLString(payload.date)) {
                    t.participants = t.participants.filter((p) => p !== payload.participant)
                }
            })
            return newState

        case "CHANGE_PARTICIPANTS":
            const turnament = newState.organization.turnaments.find((t) => dateToURLString(new Date(t.date)) === dateToURLString(payload.date))

            if (turnament) {
                turnament.participants = payload.participants
            } else {
                if (newState.userTurnament.runs.length > 0) {
                    newState.userTurnament.participants = payload.participants
                }
            }
            return newState
        case "ADD_PRINT_RESULT":
            newState.resultsToPrint = payload
            return newState
        case "ADD_PRINT_PARTICIPANT":
            newState.participantspToPrint = payload
            return newState
        case "ADD_PRINT_STICKER":
            newState.stickersToPrint = payload
            return newState
        case "CLEAR_PRINTS":
            newState.resultsToPrint = []
            newState.participantspToPrint = []
            newState.stickersToPrint = []
            return newState
        case "UPDATE_USER_TURNAMENT":
            newState.userTurnament = payload
            return newState
        default:
            return state
    }

}
export default reducer
