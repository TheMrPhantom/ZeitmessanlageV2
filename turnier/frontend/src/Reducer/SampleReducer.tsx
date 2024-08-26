const initialState: EventCreationType = {
    eventInformations: {
        name: "",
        place: "",
        informations: "",
        owner: {
            name: "",
            mail: ""
        }
    }, timeslots: {
        days: []
    }, settings: {
        optional: false,
        hasMaxParticipants: false,
        maxParticipants: 0,
        onlyOneOption: false,
        secretPoll: false,
        hasDeadline: false,
        sendResult: false
    }
}

export type EventCreationType = {
    eventInformations: {
        name: string,
        place: string,
        informations: string,
        owner: {
            name: string,
            mail: string
        }
    }, timeslots: {
        days: Array<{
            day: Date,
            times: Array<Array<Date>>,
            id: number
        }>
    },
    settings: {
        optional: boolean,
        hasMaxParticipants: boolean,
        maxParticipants: number,
        onlyOneOption: boolean,
        secretPoll: boolean,
        hasDeadline: boolean,
        sendResult: boolean
    }
}

const reducer = (state = initialState, { type, payload }: {
    type: string, payload: any | {
        day: Date,
        times: Array<Array<Date>>,
        id: number
    }
}) => {

    var newState = { ...state }
    switch (type) {
        case "ADD_DAY_TO_EVENT_CREATION":
            newState.timeslots.days.push(payload)
            return newState
        default:
            return state
    }

}
export default reducer
