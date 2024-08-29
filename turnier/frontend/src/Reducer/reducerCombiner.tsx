import { combineReducers } from "redux";
import CommonReducer from "./CommonReducer";

export type RootState = ReturnType<typeof allReducer>

const allReducer = combineReducers({
    common: CommonReducer
})

export default allReducer;