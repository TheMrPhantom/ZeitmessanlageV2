import { Divider, Paper, Rating, Stack, Typography } from '@mui/material'
import React from 'react'
import style from './runselection.module.scss'
import { Result } from '../../../types/ResponseTypes'
import { runTimeToString } from '../../Common/StaticFunctionsTyped'
import PetsIcon from '@mui/icons-material/Pets';

type Props = {
    dogname: string,
    resultA: Result,
    resultJ: Result,
    dogsLeft: number | null,
    timefaultsA: number,
    timefaultsJ: number,
    unlike: () => void
}

const Dog = (props: Props) => {

    const dogsLeftComponents = () => {
        if (props.dogsLeft !== null) {
            return (
                <>
                    <Divider />
                    <Typography variant="button">{`Noch ${props.dogsLeft} starter bis zu dir`}</Typography>
                </>
            )
        }
        return <></>
    }

    return (
        <Paper className={style.padding}>
            <Stack gap={2}>
                <Stack direction="row" gap={2} alignItems="center" justifyContent="space-between">
                    <Typography variant="h5">{props.dogname}</Typography>
                    <Rating icon={<PetsIcon />} emptyIcon={<PetsIcon />} max={1} value={1} onChange={(e, o) => { console.log(props.dogname); props.unlike() }} />
                </Stack>
                <Stack gap={1}>
                    <Stack direction="row" gap={2} justifyContent="space-between">
                        <Typography variant="h6">A Lauf</Typography>
                        <Stack direction="row" gap={1}>
                            <Stack direction="row">
                                <Typography variant="h6">⏱️</Typography>
                                <Typography variant="h6">{runTimeToString(props.resultA.time)}</Typography>
                            </Stack>
                            <Stack direction="row">
                                <Typography variant="h6">✋</Typography>
                                <Typography variant="h6">{props.resultA.time > 0 ? props.resultA.faults : "-"}</Typography>
                            </Stack>
                            <Stack direction="row">
                                <Typography variant="h6">✊</Typography>
                                <Typography variant="h6">{props.resultA.time > 0 ? props.resultA.refusals : "-"}</Typography>
                            </Stack>
                            <Stack direction="row">
                                <Typography variant="h6">⌛</Typography>
                                <Typography variant="h6">{props.timefaultsA}</Typography>
                            </Stack>
                        </Stack>
                    </Stack>
                    <Stack direction="row" gap={2} justifyContent="space-between">
                        <Typography variant="h6">Jumping</Typography>
                        <Stack direction="row" gap={1}>
                            <Stack direction="row">
                                <Typography variant="h6">⏱️</Typography>
                                <Typography variant="h6">{runTimeToString(props.resultJ.time)}</Typography>
                            </Stack>
                            <Stack direction="row">
                                <Typography variant="h6">✋</Typography>
                                <Typography variant="h6">{props.resultJ.time > 0 ? props.resultJ.faults : "-"}</Typography>
                            </Stack>
                            <Stack direction="row">
                                <Typography variant="h6">✊</Typography>
                                <Typography variant="h6">{props.resultJ.time > 0 ? props.resultJ.refusals : "-"}</Typography>
                            </Stack>
                            <Stack direction="row">
                                <Typography variant="h6">⌛</Typography>
                                <Typography variant="h6">{props.timefaultsJ}</Typography>
                            </Stack>
                        </Stack>
                    </Stack>
                </Stack>
                {dogsLeftComponents()}
            </Stack>
        </Paper >

    )
}

export default Dog