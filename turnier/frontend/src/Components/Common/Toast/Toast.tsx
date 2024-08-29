import * as React from 'react';
import Alert from "@mui/material/Alert"
import style from './toast.module.scss';
import { AlertTitle, LinearProgress, Snackbar } from '@mui/material';
import { useDispatch, useSelector } from 'react-redux';
import { CommonReducerType } from '../../../Reducer/CommonReducer';
import { RootState } from '../../../Reducer/reducerCombiner';
import { closeToast } from '../../../Actions/CommonAction';
import { useEffect, useState } from 'react';
import Spacer from '../Spacer';

type Props = {}

const Toast = (props: Props) => {
    const dispatch = useDispatch()
    const common: CommonReducerType = useSelector((state: RootState) => state.common);
    const [startTime, setstartTime] = useState(Date.now())
    const [currentTime, setcurrentTime] = useState(Date.now())

    const updateTime = React.useCallback(
        () => {
            setTimeout(() => {
                setcurrentTime(Date.now())
            }, 400);
        },
        [],
    )

    useEffect(() => {
        updateTime()
    }, [currentTime, updateTime, startTime]);

    useEffect(() => {
        setstartTime(Date.now())
    }, [common.toast.open])

    const progressValue = () => {
        const value = 1 - ((currentTime - startTime) / common.toast.duration)

        if (value < 0 || !common.toast.open) {
            return 1
        } else {
            return value
        }
    }
    
    
    return (
        <Snackbar
            open={common.toast.open}
            autoHideDuration={common.toast.duration}
            onClose={() => {
                dispatch(closeToast())
            }}
            className={style.snackbar}
        >
            <div className={style.container}>
                <Alert
                    className={style.alert}
                    severity={common.toast.type}
                    sx={{ mb: 2 }}
                >

                    {common.toast.headline ? <AlertTitle>{common.toast.headline}</AlertTitle> : <></>}
                    {common.toast.message}
                    <Spacer vertical={5} />

                    <LinearProgress variant="determinate" value={progressValue() * 100} />
                </Alert>

            </div>
        </Snackbar>
    )
}

export default Toast